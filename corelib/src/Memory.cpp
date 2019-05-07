/*
Copyright (c) 2010-2016, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <rtabmap/utilite/UEventsManager.h>
#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/utilite/UConversion.h>
#include <rtabmap/utilite/UProcessInfo.h>
#include <rtabmap/utilite/UMath.h>

#include "rtabmap/core/Memory.h"
#include "rtabmap/core/Signature.h"
#include "rtabmap/core/Parameters.h"
#include "rtabmap/core/RtabmapEvent.h"
#include "rtabmap/core/VWDictionary.h"
#include <rtabmap/core/EpipolarGeometry.h>
#include "rtabmap/core/VisualWord.h"
#include "rtabmap/core/Features2d.h"
#include "rtabmap/core/RegistrationIcp.h"
#include "rtabmap/core/Registration.h"
#include "rtabmap/core/RegistrationVis.h"
#include "rtabmap/core/DBDriver.h"
#include "rtabmap/core/util3d_features.h"
#include "rtabmap/core/util3d_filtering.h"
#include "rtabmap/core/util3d_correspondences.h"
#include "rtabmap/core/util3d_registration.h"
#include "rtabmap/core/util3d_surface.h"
#include "rtabmap/core/util3d_transforms.h"
#include "rtabmap/core/util3d_motion_estimation.h"
#include "rtabmap/core/util3d.h"
#include "rtabmap/core/util2d.h"
#include "rtabmap/core/Statistics.h"
#include "rtabmap/core/Compression.h"
#include "rtabmap/core/Graph.h"
#include "rtabmap/core/Stereo.h"
#include "rtabmap/core/optimizer/OptimizerG2O.h"
#include <pcl/io/pcd_io.h>
#include <pcl/common/common.h>
#include <rtabmap/core/OccupancyGrid.h>
#include <rtabmap/core/MarkerDetector.h>
#include <opencv2/imgproc/types_c.h>

namespace rtabmap {

const int Memory::kIdStart = 0;
const int Memory::kIdVirtual = -1;
const int Memory::kIdInvalid = 0;

Memory::Memory(const ParametersMap & parameters) :
	_dbDriver(0),
	_similarityThreshold(Parameters::defaultMemRehearsalSimilarity()),
	_binDataKept(Parameters::defaultMemBinDataKept()),
	_rawDescriptorsKept(Parameters::defaultMemRawDescriptorsKept()),
	_saveDepth16Format(Parameters::defaultMemSaveDepth16Format()),
	_notLinkedNodesKeptInDb(Parameters::defaultMemNotLinkedNodesKept()),
	_saveIntermediateNodeData(Parameters::defaultMemIntermediateNodeDataKept()),
	_rgbCompressionFormat(Parameters::defaultMemImageCompressionFormat()),
	_incrementalMemory(Parameters::defaultMemIncrementalMemory()),
	_reduceGraph(Parameters::defaultMemReduceGraph()),
	_maxStMemSize(Parameters::defaultMemSTMSize()),
	_recentWmRatio(Parameters::defaultMemRecentWmRatio()),
	_transferSortingByWeightId(Parameters::defaultMemTransferSortingByWeightId()),
	_idUpdatedToNewOneRehearsal(Parameters::defaultMemRehearsalIdUpdatedToNewOne()),
	_generateIds(Parameters::defaultMemGenerateIds()),
	_badSignaturesIgnored(Parameters::defaultMemBadSignaturesIgnored()),
	_mapLabelsAdded(Parameters::defaultMemMapLabelsAdded()),
	_depthAsMask(Parameters::defaultMemDepthAsMask()),
	_imagePreDecimation(Parameters::defaultMemImagePreDecimation()),
	_imagePostDecimation(Parameters::defaultMemImagePostDecimation()),
	_compressionParallelized(Parameters::defaultMemCompressionParallelized()),
	_laserScanDownsampleStepSize(Parameters::defaultMemLaserScanDownsampleStepSize()),
	_laserScanVoxelSize(Parameters::defaultMemLaserScanVoxelSize()),
	_laserScanNormalK(Parameters::defaultMemLaserScanNormalK()),
	_laserScanNormalRadius(Parameters::defaultMemLaserScanNormalRadius()),
	_reextractLoopClosureFeatures(Parameters::defaultRGBDLoopClosureReextractFeatures()),
	_localBundleOnLoopClosure(Parameters::defaultRGBDLocalBundleOnLoopClosure()),
	_rehearsalMaxDistance(Parameters::defaultRGBDLinearUpdate()),
	_rehearsalMaxAngle(Parameters::defaultRGBDAngularUpdate()),
	_rehearsalWeightIgnoredWhileMoving(Parameters::defaultMemRehearsalWeightIgnoredWhileMoving()),
	_useOdometryFeatures(Parameters::defaultMemUseOdomFeatures()),
	_createOccupancyGrid(Parameters::defaultRGBDCreateOccupancyGrid()),
	_visMaxFeatures(Parameters::defaultVisMaxFeatures()),
	_imagesAlreadyRectified(Parameters::defaultRtabmapImagesAlreadyRectified()),
	_rectifyOnlyFeatures(Parameters::defaultRtabmapRectifyOnlyFeatures()),
	_covOffDiagonalIgnored(Parameters::defaultMemCovOffDiagIgnored()),
	_detectMarkers(Parameters::defaultRGBDMarkerDetection()),
	_markerLinVariance(Parameters::defaultMarkerVarianceLinear()),
	_markerAngVariance(Parameters::defaultMarkerVarianceAngular()),
	_idCount(kIdStart),
	_idMapCount(kIdStart),
	_lastSignature(0),
	_lastGlobalLoopClosureId(0),
	_memoryChanged(false),
	_linksChanged(false),
	_signaturesAdded(0),
	_allNodesInWM(true),

	_badSignRatio(Parameters::defaultKpBadSignRatio()),
	_tfIdfLikelihoodUsed(Parameters::defaultKpTfIdfLikelihoodUsed()),
	_parallelized(Parameters::defaultKpParallelized())
{
	_feature2D = Feature2D::create(parameters);
	_vwd = new VWDictionary(parameters);
	_registrationPipeline = Registration::create(parameters);

	// for local scan matching, correspondences ratio should be two times higher as we expect more matches
	float corRatio = Parameters::defaultIcpCorrespondenceRatio();
	Parameters::parse(parameters, Parameters::kIcpCorrespondenceRatio(), corRatio);
	ParametersMap paramsMulti = parameters;
	paramsMulti.insert(ParametersPair(Parameters::kIcpCorrespondenceRatio(), uNumber2Str(corRatio>=0.5f?1.0f:corRatio*2.0f)));
	if(corRatio >= 0.5)
	{
		UWARN(	"%s is >=0.5, which sets correspondence ratio for proximity detection using "
			"laser scans to 100% (2 x Ratio). You may lower the ratio to accept proximity "
			"detection with not full scans overlapping.", Parameters::kIcpCorrespondenceRatio().c_str());
	}
	_registrationIcpMulti = new RegistrationIcp(paramsMulti);

	_occupancy = new OccupancyGrid(parameters);
	_markerDetector = new MarkerDetector(parameters);
	this->parseParameters(parameters);
}

bool Memory::init(const std::string & dbUrl, bool dbOverwritten, const ParametersMap & parameters, bool postInitClosingEvents)
{
	if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(RtabmapEventInit::kInitializing));

	UDEBUG("");
	this->parseParameters(parameters);

	if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Clearing memory..."));
	DBDriver * tmpDriver = 0;
	if((!_memoryChanged && !_linksChanged) || dbOverwritten)
	{
		if(_dbDriver)
		{
			tmpDriver = _dbDriver;
			_dbDriver = 0; // HACK for the clear() below to think that there is no db
		}
	}
	else if(!_memoryChanged && _linksChanged)
	{
		_dbDriver->setTimestampUpdateEnabled(false); // update links only
	}
	this->clear();
	if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Clearing memory, done!"));

	if(tmpDriver)
	{
		_dbDriver = tmpDriver;
	}

	if(_dbDriver)
	{
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Closing database connection..."));
		_dbDriver->closeConnection();
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Closing database connection, done!"));
	}

	if(_dbDriver == 0)
	{
		_dbDriver = DBDriver::create(parameters);
	}

	bool success = true;
	if(_dbDriver)
	{
		_dbDriver->setTimestampUpdateEnabled(true); // make sure that timestamp update is enabled (may be disabled above)
		success = false;
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(std::string("Connecting to database \"") + dbUrl + "\"..."));
		if(_dbDriver->openConnection(dbUrl, dbOverwritten))
		{
			success = true;
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(std::string("Connecting to database \"") + dbUrl + "\", done!"));
		}
		else
		{
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(RtabmapEventInit::kError, std::string("Connecting to database ") + dbUrl + ", path is invalid!"));
		}
	}

	loadDataFromDb(postInitClosingEvents);

	if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(RtabmapEventInit::kInitialized));

	return success;
}

void Memory::loadDataFromDb(bool postInitClosingEvents)
{
	if(_dbDriver && _dbDriver->isConnected())
	{
		bool loadAllNodesInWM = Parameters::defaultMemInitWMWithAllNodes();
		Parameters::parse(parameters_, Parameters::kMemInitWMWithAllNodes(), loadAllNodesInWM);

		// Load the last working memory...
		std::list<Signature*> dbSignatures;

		if(loadAllNodesInWM)
		{
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(std::string("Loading all nodes to WM...")));
			std::set<int> ids;
			_dbDriver->getAllNodeIds(ids, true);
			_dbDriver->loadSignatures(std::list<int>(ids.begin(), ids.end()), dbSignatures);
		}
		else
		{
			// load previous session working memory
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(std::string("Loading last nodes to WM...")));
			_dbDriver->loadLastNodes(dbSignatures);
		}
		for(std::list<Signature*>::reverse_iterator iter=dbSignatures.rbegin(); iter!=dbSignatures.rend(); ++iter)
		{
			// ignore bad signatures
			if(!((*iter)->isBadSignature() && _badSignaturesIgnored))
			{
				// insert all in WM
				// Note: it doesn't make sense to keep last STM images
				//       of the last session in the new STM because they can be
				//       only linked with the ones of the current session by
				//       global loop closures.
				_signatures.insert(std::pair<int, Signature *>((*iter)->id(), *iter));
				_workingMem.insert(std::make_pair((*iter)->id(), UTimer::now()));
				if(!(*iter)->getGroundTruthPose().isNull()) {
					_groundTruths.insert(std::make_pair((*iter)->id(), (*iter)->getGroundTruthPose()));
				}

				if(!(*iter)->getLandmarks().empty())
				{
					// Update landmark indexes
					for(std::map<int, Link>::const_iterator jter = (*iter)->getLandmarks().begin(); jter!=(*iter)->getLandmarks().end(); ++jter)
					{
						int landmarkId = jter->first;
						UASSERT(landmarkId < 0);

						std::map<int, std::set<int> >::iterator nter = _landmarksIndex.find((*iter)->id());
						if(nter!=_landmarksIndex.end())
						{
							nter->second.insert(landmarkId);
						}
						else
						{
							std::set<int> tmp;
							tmp.insert(landmarkId);
							_landmarksIndex.insert(std::make_pair((*iter)->id(), tmp));
						}
						nter = _landmarksInvertedIndex.find(landmarkId);
						if(nter!=_landmarksInvertedIndex.end())
						{
							nter->second.insert((*iter)->id());
						}
						else
						{
							std::set<int> tmp;
							tmp.insert((*iter)->id());
							_landmarksInvertedIndex.insert(std::make_pair(landmarkId, tmp));
						}
					}
				}
			}
			else
			{
				delete *iter;
			}
		}

		// Get labels
		UDEBUG("Get labels");
		_dbDriver->getAllLabels(_labels);

		UDEBUG("Check if all nodes are in Working Memory");
		for(std::map<int, Signature*>::iterator iter=_signatures.begin(); iter!=_signatures.end() && _allNodesInWM; ++iter)
		{
			for(std::map<int, Link>::const_iterator jter = iter->second->getLinks().begin(); jter!=iter->second->getLinks().end(); ++jter)
			{
				if(_signatures.find(jter->first) == _signatures.end())
				{
					_allNodesInWM = false;
					break;
				}
			}
		}
		UDEBUG("allNodesInWM()=%s", _allNodesInWM?"true":"false");

		UDEBUG("update odomMaxInf vector");
		std::multimap<int, Link> links = this->getAllLinks(true, true);
		for(std::multimap<int, Link>::iterator iter=links.begin(); iter!=links.end(); ++iter)
		{
			if(iter->second.type() == Link::kNeighbor &&
			   iter->second.infMatrix().cols == 6 &&
			   iter->second.infMatrix().rows == 6)
			{
				if(_odomMaxInf.empty())
				{
					_odomMaxInf.resize(6, 0.0);
				}
				for(int i=0; i<6; ++i)
				{
					const double & v = iter->second.infMatrix().at<double>(i,i);
					if(_odomMaxInf[i] < v)
					{
						_odomMaxInf[i] = v;
					}
				}
			}
		}
		UDEBUG("update odomMaxInf vector, done!");

		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(std::string("Loading nodes to WM, done! (") + uNumber2Str(int(_workingMem.size() + _stMem.size())) + " loaded)"));

		// Assign the last signature
		if(_stMem.size()>0)
		{
			_lastSignature = uValue(_signatures, *_stMem.rbegin(), (Signature*)0);
		}
		else if(_workingMem.size()>0)
		{
			_lastSignature = uValue(_signatures, _workingMem.rbegin()->first, (Signature*)0);
		}

		// Last id
		_dbDriver->getLastNodeId(_idCount);
		_idMapCount = _lastSignature?_lastSignature->mapId()+1:kIdStart;

		// Now load the dictionary if we have a connection
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Loading dictionary..."));
		UDEBUG("Loading dictionary...");
		if(loadAllNodesInWM)
		{
			UDEBUG("load all referenced words in working memory");
			// load all referenced words in working memory
			std::set<int> wordIds;
			const std::map<int, Signature *> & signatures = this->getSignatures();
			for(std::map<int, Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
			{
				const std::multimap<int, cv::KeyPoint> & words = i->second->getWords();
				std::list<int> keys = uUniqueKeys(words);
				for(std::list<int>::iterator iter=keys.begin(); iter!=keys.end(); ++iter)
				{
					if(*iter > 0)
					{
						wordIds.insert(*iter);
					}
				}
			}

			UDEBUG("load words %d", (int)wordIds.size());
			if(_vwd->isIncremental())
			{
				if(wordIds.size())
				{
					std::list<VisualWord*> words;
					_dbDriver->loadWords(wordIds, words);
					for(std::list<VisualWord*>::iterator iter = words.begin(); iter!=words.end(); ++iter)
					{
						_vwd->addWord(*iter);
					}
					// Get Last word id
					int id = 0;
					_dbDriver->getLastWordId(id);
					_vwd->setLastWordId(id);
				}
			}
			else
			{
				_dbDriver->load(_vwd, false);
			}
		}
		else
		{
			UDEBUG("load words");
			// load the last dictionary
			_dbDriver->load(_vwd, _vwd->isIncremental());
		}
		UDEBUG("%d words loaded!", _vwd->getUnusedWordsSize());
		_vwd->update();
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(uFormat("Loading dictionary, done! (%d words)", (int)_vwd->getUnusedWordsSize())));

		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(std::string("Adding word references...")));
		// Enable loaded signatures
		const std::map<int, Signature *> & signatures = this->getSignatures();
		for(std::map<int, Signature *>::const_iterator i=signatures.begin(); i!=signatures.end(); ++i)
		{
			Signature * s = this->_getSignature(i->first);
			UASSERT(s != 0);

			const std::multimap<int, cv::KeyPoint> & words = s->getWords();
			if(words.size())
			{
				UDEBUG("node=%d, word references=%d", s->id(), words.size());
				for(std::multimap<int, cv::KeyPoint>::const_iterator iter = words.begin(); iter!=words.end(); ++iter)
				{
					if(iter->first > 0)
					{
						_vwd->addWordRef(iter->first, i->first);
					}
				}
				s->setEnabled(true);
			}
		}
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(uFormat("Adding word references, done! (%d)", _vwd->getTotalActiveReferences())));

		if(_vwd->getUnusedWordsSize() && _vwd->isIncremental())
		{
			UWARN("_vwd->getUnusedWordsSize() must be empty... size=%d", _vwd->getUnusedWordsSize());
		}
		UDEBUG("Total word references added = %d", _vwd->getTotalActiveReferences());

		if(_lastSignature == 0)
		{
			// Memory is empty, save parameters
			ParametersMap parameters = Parameters::getDefaultParameters();
			uInsert(parameters, parameters_);
			parameters.erase(Parameters::kRtabmapWorkingDirectory()); // don't save working directory as it is machine dependent
			UDEBUG("");
			_dbDriver->addInfoAfterRun(0, 0, 0, 0, 0, parameters);
		}
	}
	else
	{
		_idCount = kIdStart;
		_idMapCount = kIdStart;
	}

	_workingMem.insert(std::make_pair(kIdVirtual, 0));

	UDEBUG("ids start with %d", _idCount+1);
	UDEBUG("map ids start with %d", _idMapCount);
}

void Memory::close(bool databaseSaved, bool postInitClosingEvents, const std::string & ouputDatabasePath)
{
	UINFO("databaseSaved=%d, postInitClosingEvents=%d", databaseSaved?1:0, postInitClosingEvents?1:0);
	if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(RtabmapEventInit::kClosing));

	bool databaseNameChanged = false;
	if(databaseSaved && _dbDriver)
	{
		databaseNameChanged = ouputDatabasePath.size() && _dbDriver->getUrl().size() && _dbDriver->getUrl().compare(ouputDatabasePath) != 0?true:false;
	}

	if(!databaseSaved || (!_memoryChanged && !_linksChanged && !databaseNameChanged))
	{
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(uFormat("No changes added to database.")));

		UINFO("No changes added to database.");
		if(_dbDriver)
		{
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(uFormat("Closing database \"%s\"...", _dbDriver->getUrl().c_str())));
			_dbDriver->closeConnection(false, ouputDatabasePath);
			delete _dbDriver;
			_dbDriver = 0;
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Closing database, done!"));
		}
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Clearing memory..."));
		this->clear();
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Clearing memory, done!"));
	}
	else
	{
		UINFO("Saving memory...");
		if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Saving memory..."));
		if(!_memoryChanged && _linksChanged && _dbDriver)
		{
			// don't update the time stamps!
			UDEBUG("");
			_dbDriver->setTimestampUpdateEnabled(false);
		}
		this->clear();
		if(_dbDriver)
		{
			_dbDriver->emptyTrashes();
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Saving memory, done!"));
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(uFormat("Closing database \"%s\"...", _dbDriver->getUrl().c_str())));
			_dbDriver->closeConnection(true, ouputDatabasePath);
			delete _dbDriver;
			_dbDriver = 0;
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Closing database, done!"));
		}
		else
		{
			if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit("Saving memory, done!"));
		}
	}
	if(postInitClosingEvents) UEventsManager::post(new RtabmapEventInit(RtabmapEventInit::kClosed));
}

Memory::~Memory()
{
	this->close();

	if(_dbDriver)
	{
		UWARN("Please call Memory::close() before");
	}
	delete _feature2D;
	delete _vwd;
	delete _registrationPipeline;
	delete _registrationIcpMulti;
	delete _occupancy;
}

void Memory::parseParameters(const ParametersMap & parameters)
{
	uInsert(parameters_, parameters);
	ParametersMap params = parameters;

	UDEBUG("");
	ParametersMap::const_iterator iter;

	Parameters::parse(params, Parameters::kMemBinDataKept(), _binDataKept);
	Parameters::parse(params, Parameters::kMemRawDescriptorsKept(), _rawDescriptorsKept);
	Parameters::parse(params, Parameters::kMemSaveDepth16Format(), _saveDepth16Format);
	Parameters::parse(params, Parameters::kMemReduceGraph(), _reduceGraph);
	Parameters::parse(params, Parameters::kMemNotLinkedNodesKept(), _notLinkedNodesKeptInDb);
	Parameters::parse(params, Parameters::kMemIntermediateNodeDataKept(), _saveIntermediateNodeData);
	Parameters::parse(params, Parameters::kMemImageCompressionFormat(), _rgbCompressionFormat);
	Parameters::parse(params, Parameters::kMemRehearsalIdUpdatedToNewOne(), _idUpdatedToNewOneRehearsal);
	Parameters::parse(params, Parameters::kMemGenerateIds(), _generateIds);
	Parameters::parse(params, Parameters::kMemBadSignaturesIgnored(), _badSignaturesIgnored);
	Parameters::parse(params, Parameters::kMemMapLabelsAdded(), _mapLabelsAdded);
	Parameters::parse(params, Parameters::kMemRehearsalSimilarity(), _similarityThreshold);
	Parameters::parse(params, Parameters::kMemRecentWmRatio(), _recentWmRatio);
	Parameters::parse(params, Parameters::kMemTransferSortingByWeightId(), _transferSortingByWeightId);
	Parameters::parse(params, Parameters::kMemSTMSize(), _maxStMemSize);
	Parameters::parse(params, Parameters::kMemDepthAsMask(), _depthAsMask);
	Parameters::parse(params, Parameters::kMemImagePreDecimation(), _imagePreDecimation);
	Parameters::parse(params, Parameters::kMemImagePostDecimation(), _imagePostDecimation);
	Parameters::parse(params, Parameters::kMemCompressionParallelized(), _compressionParallelized);
	Parameters::parse(params, Parameters::kMemLaserScanDownsampleStepSize(), _laserScanDownsampleStepSize);
	Parameters::parse(params, Parameters::kMemLaserScanVoxelSize(), _laserScanVoxelSize);
	Parameters::parse(params, Parameters::kMemLaserScanNormalK(), _laserScanNormalK);
	Parameters::parse(params, Parameters::kMemLaserScanNormalRadius(), _laserScanNormalRadius);
	Parameters::parse(params, Parameters::kRGBDLoopClosureReextractFeatures(), _reextractLoopClosureFeatures);
	Parameters::parse(params, Parameters::kRGBDLocalBundleOnLoopClosure(), _localBundleOnLoopClosure);
	Parameters::parse(params, Parameters::kRGBDLinearUpdate(), _rehearsalMaxDistance);
	Parameters::parse(params, Parameters::kRGBDAngularUpdate(), _rehearsalMaxAngle);
	Parameters::parse(params, Parameters::kMemRehearsalWeightIgnoredWhileMoving(), _rehearsalWeightIgnoredWhileMoving);
	Parameters::parse(params, Parameters::kMemUseOdomFeatures(), _useOdometryFeatures);
	Parameters::parse(params, Parameters::kRGBDCreateOccupancyGrid(), _createOccupancyGrid);
	Parameters::parse(params, Parameters::kVisMaxFeatures(), _visMaxFeatures);
	Parameters::parse(params, Parameters::kRtabmapImagesAlreadyRectified(), _imagesAlreadyRectified);
	Parameters::parse(params, Parameters::kRtabmapRectifyOnlyFeatures(), _rectifyOnlyFeatures);
	Parameters::parse(params, Parameters::kMemCovOffDiagIgnored(), _covOffDiagonalIgnored);
	Parameters::parse(params, Parameters::kRGBDMarkerDetection(), _detectMarkers);
	Parameters::parse(params, Parameters::kMarkerVarianceLinear(), _markerLinVariance);
	Parameters::parse(params, Parameters::kMarkerVarianceAngular(), _markerAngVariance);

	UASSERT_MSG(_maxStMemSize >= 0, uFormat("value=%d", _maxStMemSize).c_str());
	UASSERT_MSG(_similarityThreshold >= 0.0f && _similarityThreshold <= 1.0f, uFormat("value=%f", _similarityThreshold).c_str());
	UASSERT_MSG(_recentWmRatio >= 0.0f && _recentWmRatio <= 1.0f, uFormat("value=%f", _recentWmRatio).c_str());
	if(_imagePreDecimation == 0)
	{
		_imagePreDecimation = 1;
	}
	if(_imagePostDecimation == 0)
	{
		_imagePostDecimation = 1;
	}
	UASSERT(_rehearsalMaxDistance >= 0.0f);
	UASSERT(_rehearsalMaxAngle >= 0.0f);

	if(_dbDriver)
	{
		_dbDriver->parseParameters(params);
	}

	// Keypoint stuff
	if(_vwd)
	{
		_vwd->parseParameters(params);
	}

	Parameters::parse(params, Parameters::kKpTfIdfLikelihoodUsed(), _tfIdfLikelihoodUsed);
	Parameters::parse(params, Parameters::kKpParallelized(), _parallelized);
	Parameters::parse(params, Parameters::kKpBadSignRatio(), _badSignRatio);

	//Keypoint detector
	UASSERT(_feature2D != 0);
	Feature2D::Type detectorStrategy = Feature2D::kFeatureUndef;
	if((iter=params.find(Parameters::kKpDetectorStrategy())) != params.end())
	{
		detectorStrategy = (Feature2D::Type)std::atoi((*iter).second.c_str());
	}
	if(detectorStrategy!=Feature2D::kFeatureUndef && detectorStrategy!=_feature2D->getType())
	{
		if(_vwd->getVisualWords().size())
		{
			UWARN("new detector strategy %d while the vocabulary is already created. This may give problems if feature descriptors are not the same type than the one used in the current vocabulary (a memory reset would be required if so).", int(detectorStrategy));
		}
		else
		{
			UINFO("new detector strategy %d.", int(detectorStrategy));
		}
		if(_feature2D)
		{
			delete _feature2D;
			_feature2D = 0;
		}

		_feature2D = Feature2D::create(detectorStrategy, parameters_);
	}
	else if(_feature2D)
	{
		_feature2D->parseParameters(params);
	}

	// Features Matching is the only correspondence approach supported for loop closure transformation estimation.
	uInsert(parameters_, ParametersPair(Parameters::kVisCorType(), "0"));
	uInsert(params, ParametersPair(Parameters::kVisCorType(), "0"));

	Registration::Type regStrategy = Registration::kTypeUndef;
	if((iter=params.find(Parameters::kRegStrategy())) != params.end())
	{
		regStrategy = (Registration::Type)std::atoi((*iter).second.c_str());
	}
	if(regStrategy!=Registration::kTypeUndef)
	{
		UDEBUG("new registration strategy %d", int(regStrategy));
		if(_registrationPipeline)
		{
			delete _registrationPipeline;
			_registrationPipeline = 0;
		}

		_registrationPipeline = Registration::create(regStrategy, parameters_);
	}
	else if(_registrationPipeline)
	{
		_registrationPipeline->parseParameters(params);
	}

	if(_registrationIcpMulti)
	{
		if(uContains(params, Parameters::kIcpCorrespondenceRatio()))
		{
			// for local scan matching, correspondences ratio should be two times higher as we expect more matches
			float corRatio = Parameters::defaultIcpCorrespondenceRatio();
			Parameters::parse(parameters, Parameters::kIcpCorrespondenceRatio(), corRatio);
			ParametersMap paramsMulti = params;
			uInsert(paramsMulti, ParametersPair(Parameters::kIcpCorrespondenceRatio(), uNumber2Str(corRatio>=0.5f?1.0f:corRatio*2.0f)));
			if(corRatio >= 0.5)
			{
				UWARN(	"%s is >=0.5, which sets correspondence ratio for proximity detection using "
					"laser scans to 100% (2 x Ratio). You may lower the ratio to accept proximity "
					"detection with not full scans overlapping.", Parameters::kIcpCorrespondenceRatio().c_str());
			}
			_registrationIcpMulti->parseParameters(paramsMulti);
		}
		else
		{
			_registrationIcpMulti->parseParameters(params);
		}
	}

	if(_occupancy)
	{
		_occupancy->parseParameters(params);
	}

	if(_markerDetector)
	{
		_markerDetector->parseParameters(params);
	}

	// do this after all params are parsed
	// SLAM mode vs Localization mode
	iter = params.find(Parameters::kMemIncrementalMemory());
	if(iter != params.end())
	{
		bool value = uStr2Bool(iter->second.c_str());
		if(value == false && _incrementalMemory)
		{
			// From SLAM to localization, change map id
			this->incrementMapId();

			// The easiest way to make sure that the mapping session is saved
			// is to save the memory in the database and reload it.
			if((_memoryChanged || _linksChanged) && _dbDriver)
			{
				UWARN("Switching from Mapping to Localization mode, the database will be saved and reloaded.");
				bool memoryChanged = _memoryChanged;
				bool linksChanged = _linksChanged;
				this->clear();
				_memoryChanged = memoryChanged;
				_linksChanged = linksChanged;
				this->loadDataFromDb(false);
				UWARN("Switching from Mapping to Localization mode, the database is reloaded!");
			}
		}
		_incrementalMemory = value;
	}

	if(_useOdometryFeatures)
	{
		int visFeatureType = Parameters::defaultVisFeatureType();
		int kpDetectorStrategy = Parameters::defaultKpDetectorStrategy();
		Parameters::parse(parameters_, Parameters::kVisFeatureType(), visFeatureType);
		Parameters::parse(parameters_, Parameters::kKpDetectorStrategy(), kpDetectorStrategy);
		if(visFeatureType != kpDetectorStrategy)
		{
			UWARN("%s is enabled, but %s and %s parameters are not the same! Disabling %s...",
					Parameters::kMemUseOdomFeatures().c_str(),
					Parameters::kVisFeatureType().c_str(),
					Parameters::kKpDetectorStrategy().c_str(),
					Parameters::kMemUseOdomFeatures().c_str());
			_useOdometryFeatures = false;
			uInsert(parameters_, ParametersPair(Parameters::kMemUseOdomFeatures(), "false"));
		}
	}
}

void Memory::preUpdate()
{
	_signaturesAdded = 0;
	if(_vwd->isIncremental())
	{
		this->cleanUnusedWords();
	}
	if(_vwd && !_parallelized)
	{
		//When parallelized, it is done in CreateSignature
		_vwd->update();
	}
}

bool Memory::update(
		const SensorData & data,
		Statistics * stats)
{
	return update(data, Transform(), cv::Mat(), std::vector<float>(), stats);
}

bool Memory::update(
		const SensorData & data,
		const Transform & pose,
		const cv::Mat & covariance,
		const std::vector<float> & velocity,
		Statistics * stats)
{
	UDEBUG("");
	UTimer timer;
	UTimer totalTimer;
	timer.start();
	float t;

	//============================================================
	// Pre update...
	//============================================================
	UDEBUG("pre-updating...");
	this->preUpdate();
	t=timer.ticks()*1000;
	if(stats) stats->addStatistic(Statistics::kTimingMemPre_update(), t);
	UDEBUG("time preUpdate=%f ms", t);

	//============================================================
	// Create a signature with the image received.
	//============================================================
	Signature * signature = this->createSignature(data, pose, stats);
	if (signature == 0)
	{
		UERROR("Failed to create a signature...");
		return false;
	}
	if(velocity.size()==6)
	{
		signature->setVelocity(velocity[0], velocity[1], velocity[2], velocity[3], velocity[4], velocity[5]);
	}

	t=timer.ticks()*1000;
	if(stats) stats->addStatistic(Statistics::kTimingMemSignature_creation(), t);
	UDEBUG("time creating signature=%f ms", t);

	// It will be added to the short-term memory, no need to delete it...
	this->addSignatureToStm(signature, covariance);

	_lastSignature = signature;

	//============================================================
	// Rehearsal step...
	// Compare with the X last signatures. If different, add this
	// signature like a parent to the memory tree, otherwise add
	// it as a child to the similar signature.
	//============================================================
	if(_incrementalMemory)
	{
		if(_similarityThreshold < 1.0f)
		{
			this->rehearsal(signature, stats);
		}
		t=timer.ticks()*1000;
		if(stats) stats->addStatistic(Statistics::kTimingMemRehearsal(), t);
		UDEBUG("time rehearsal=%f ms", t);
	}
	else
	{
		if(_workingMem.size() <= 1)
		{
			UWARN("The working memory is empty and the memory is not "
				  "incremental (Mem/IncrementalMemory=False), no loop closure "
				  "can be detected! Please set Mem/IncrementalMemory=true to increase "
				  "the memory with new images or decrease the STM size (which is %d "
				  "including the new one added).", (int)_stMem.size());
		}
	}

	//============================================================
	// Transfer the oldest signature of the short-term memory to the working memory
	//============================================================
	int notIntermediateNodesCount = 0;
	for(std::set<int>::iterator iter=_stMem.begin(); iter!=_stMem.end(); ++iter)
	{
		const Signature * s = this->getSignature(*iter);
		UASSERT(s != 0);
		if(s->getWeight() >= 0)
		{
			++notIntermediateNodesCount;
		}
	}
	std::map<int, int> reducedIds;
	while(_stMem.size() && _maxStMemSize>0 && notIntermediateNodesCount > _maxStMemSize)
	{
		int id = *_stMem.begin();
		Signature * s = this->_getSignature(id);
		UASSERT(s != 0);
		if(s->getWeight() >= 0)
		{
			--notIntermediateNodesCount;
		}

		int reducedTo = 0;
		moveSignatureToWMFromSTM(id, &reducedTo);

		if(reducedTo > 0)
		{
			reducedIds.insert(std::make_pair(id, reducedTo));
		}
	}
	if(stats) stats->setReducedIds(reducedIds);

	if(!_memoryChanged && _incrementalMemory)
	{
		_memoryChanged = true;
	}

	UDEBUG("totalTimer = %fs", totalTimer.ticks());

	return true;
}

void Memory::addSignatureToStm(Signature * signature, const cv::Mat & covariance)
{
	UTimer timer;
	// add signature on top of the short-term memory
	if(signature)
	{
		UDEBUG("adding %d", signature->id());
		// Update neighbors
		if(_stMem.size())
		{
			if(_signatures.at(*_stMem.rbegin())->mapId() == signature->mapId())
			{
				Transform motionEstimate;
				if(!signature->getPose().isNull() &&
				   !_signatures.at(*_stMem.rbegin())->getPose().isNull())
				{
					UASSERT(covariance.cols == 6 && covariance.rows == 6 && covariance.type() == CV_64FC1);
					double maxAngVar = 0.0;
					if(_registrationPipeline->force3DoF())
					{
						maxAngVar = covariance.at<double>(5,5);
					}
					else
					{
						maxAngVar = uMax3(covariance.at<double>(3,3), covariance.at<double>(4,4), covariance.at<double>(5,5));
					}

					if(maxAngVar != 1.0 && maxAngVar > 0.1)
					{
						static bool warned = false;
						if(!warned)
						{
							UWARN("Very large angular variance (%f) detected! Please fix odometry "
									"covariance, otherwise poor graph optimizations are "
									"expected and wrong loop closure detections creating a lot "
									"of errors in the map could be accepted. This message is only "
									"printed once.", maxAngVar);
							warned = true;
						}
					}

					cv::Mat infMatrix;
					if(_covOffDiagonalIgnored)
					{
						infMatrix = cv::Mat::zeros(6,6,CV_64FC1);
						infMatrix.at<double>(0,0) = 1.0 / covariance.at<double>(0,0);
						infMatrix.at<double>(1,1) = 1.0 / covariance.at<double>(1,1);
						infMatrix.at<double>(2,2) = 1.0 / covariance.at<double>(2,2);
						infMatrix.at<double>(3,3) = 1.0 / covariance.at<double>(3,3);
						infMatrix.at<double>(4,4) = 1.0 / covariance.at<double>(4,4);
						infMatrix.at<double>(5,5) = 1.0 / covariance.at<double>(5,5);
					}
					else
					{
						infMatrix = covariance.inv();
					}
					if((uIsFinite(covariance.at<double>(0,0)) && covariance.at<double>(0,0)>0.0) &&
						!(uIsFinite(infMatrix.at<double>(0,0)) && infMatrix.at<double>(0,0)>0.0))
					{
						UERROR("Failed to invert the covariance matrix! Covariance matrix should be invertible!");
						std::cout << "Covariance: " << covariance << std::endl;
						infMatrix = cv::Mat::eye(6,6,CV_64FC1);
					}

					UASSERT(infMatrix.rows == 6 && infMatrix.cols == 6);
					if(_odomMaxInf.empty())
					{
						_odomMaxInf.resize(6, 0.0);
					}
					for(int i=0; i<6; ++i)
					{
						const double & v = infMatrix.at<double>(i,i);
						if(_odomMaxInf[i] < v)
						{
							_odomMaxInf[i] = v;
						}
					}

					motionEstimate = _signatures.at(*_stMem.rbegin())->getPose().inverse() * signature->getPose();
					_signatures.at(*_stMem.rbegin())->addLink(Link(*_stMem.rbegin(), signature->id(), Link::kNeighbor, motionEstimate, infMatrix));
					signature->addLink(Link(signature->id(), *_stMem.rbegin(), Link::kNeighbor, motionEstimate.inverse(), infMatrix));
				}
				else
				{
					_signatures.at(*_stMem.rbegin())->addLink(Link(*_stMem.rbegin(), signature->id(), Link::kNeighbor, Transform()));
					signature->addLink(Link(signature->id(), *_stMem.rbegin(), Link::kNeighbor, Transform()));
				}
				UDEBUG("Min STM id = %d", *_stMem.begin());
			}
			else
			{
				UDEBUG("Ignoring neighbor link between %d and %d because they are not in the same map! (%d vs %d)",
						*_stMem.rbegin(), signature->id(),
						_signatures.at(*_stMem.rbegin())->mapId(), signature->mapId());

				if(_mapLabelsAdded && isIncremental())
				{
					//Tag the first node of the map
					std::string tag = uFormat("map%d", signature->mapId());
					if(getSignatureIdByLabel(tag, false) == 0)
					{
						UINFO("Tagging node %d with label \"%s\"", signature->id(), tag.c_str());
						signature->setLabel(tag);
						_labels.insert(std::make_pair(signature->id(), tag));
					}
				}
			}
		}
		else if(_mapLabelsAdded && isIncremental())
		{
			//Tag the first node of the map
			std::string tag = uFormat("map%d", signature->mapId());
			if(getSignatureIdByLabel(tag, false) == 0)
			{
				UINFO("Tagging node %d with label \"%s\"", signature->id(), tag.c_str());
				signature->setLabel(tag);
				_labels.insert(std::make_pair(signature->id(), tag));
			}
		}

		_signatures.insert(_signatures.end(), std::pair<int, Signature *>(signature->id(), signature));
		_stMem.insert(_stMem.end(), signature->id());
		if(!signature->getGroundTruthPose().isNull()) {
			_groundTruths.insert(std::make_pair(signature->id(), signature->getGroundTruthPose()));
		}
		++_signaturesAdded;

		if(_vwd)
		{
			UDEBUG("%d words ref for the signature %d", signature->getWords().size(), signature->id());
		}
		if(signature->getWords().size())
		{
			signature->setEnabled(true);
		}
	}

	UDEBUG("time = %fs", timer.ticks());
}

void Memory::addSignatureToWmFromLTM(Signature * signature)
{
	if(signature)
	{
		UDEBUG("Inserting node %d in WM...", signature->id());
		_workingMem.insert(std::make_pair(signature->id(), UTimer::now()));
		_signatures.insert(std::pair<int, Signature*>(signature->id(), signature));
		if(!signature->getGroundTruthPose().isNull()) {
			_groundTruths.insert(std::make_pair(signature->id(), signature->getGroundTruthPose()));
		}
		++_signaturesAdded;
	}
	else
	{
		UERROR("Signature is null ?!?");
	}
}

void Memory::moveSignatureToWMFromSTM(int id, int * reducedTo)
{
	UDEBUG("Inserting node %d from STM in WM...", id);
	UASSERT(_stMem.find(id) != _stMem.end());
	Signature * s = this->_getSignature(id);
	UASSERT(s!=0);

	if(_reduceGraph)
	{
		bool merge = false;
		const std::map<int, Link> & links = s->getLinks();
		std::map<int, Link> neighbors;
		for(std::map<int, Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
		{
			if(!merge)
			{
				merge = iter->second.to() < s->id() && // should be a parent->child link
						iter->second.to() != iter->second.from() &&
						iter->second.type() != Link::kNeighbor &&
						iter->second.type() != Link::kNeighborMerged &&
						iter->second.userDataCompressed().empty() &&
						iter->second.type() != Link::kUndef &&
						iter->second.type() != Link::kVirtualClosure;
				if(merge)
				{
					UDEBUG("Reduce %d to %d", s->id(), iter->second.to());
					if(reducedTo)
					{
						*reducedTo = iter->second.to();
					}
				}

			}
			if(iter->second.type() == Link::kNeighbor)
			{
				neighbors.insert(*iter);
			}
		}
		if(merge)
		{
			if(s->getLabel().empty())
			{
				for(std::map<int, Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
				{
					merge = true;
					Signature * sTo = this->_getSignature(iter->first);
					UASSERT(sTo!=0);
					sTo->removeLink(s->id());
					if(iter->second.type() != Link::kNeighbor &&
					   iter->second.type() != Link::kNeighborMerged &&
					   iter->second.type() != Link::kUndef)
					{
						// link to all neighbors
						for(std::map<int, Link>::iterator jter=neighbors.begin(); jter!=neighbors.end(); ++jter)
						{
							if(!sTo->hasLink(jter->second.to()))
							{
								Link l = iter->second.inverse().merge(
										jter->second,
										iter->second.userDataCompressed().empty() && iter->second.type() != Link::kVirtualClosure?Link::kNeighborMerged:iter->second.type());
								sTo->addLink(l);
								Signature * sB = this->_getSignature(l.to());
								UASSERT(sB!=0);
								UASSERT(!sB->hasLink(l.to()));
								sB->addLink(l.inverse());
							}
						}
					}
				}

				//remove neighbor links
				std::map<int, Link> linksCopy = links;
				for(std::map<int, Link>::iterator iter=linksCopy.begin(); iter!=linksCopy.end(); ++iter)
				{
					if(iter->second.type() == Link::kNeighbor ||
					   iter->second.type() == Link::kNeighborMerged)
					{
						s->removeLink(iter->first);
						if(iter->second.type() == Link::kNeighbor)
						{
							if(_lastGlobalLoopClosureId == s->id())
							{
								_lastGlobalLoopClosureId = iter->first;
							}
						}
					}
				}

				this->moveToTrash(s, _notLinkedNodesKeptInDb);
				s = 0;
			}
		}
	}
	if(s != 0)
	{
		_workingMem.insert(_workingMem.end(), std::make_pair(*_stMem.begin(), UTimer::now()));
		_stMem.erase(*_stMem.begin());
	}
	// else already removed from STM/WM in moveToTrash()
}

const Signature * Memory::getSignature(int id) const
{
	return _getSignature(id);
}

Signature * Memory::_getSignature(int id) const
{
	return uValue(_signatures, id, (Signature*)0);
}

const VWDictionary * Memory::getVWDictionary() const
{
	return _vwd;
}

std::map<int, Link> Memory::getNeighborLinks(
		int signatureId,
		bool lookInDatabase) const
{
	std::map<int, Link> links;
	Signature * s = uValue(_signatures, signatureId, (Signature*)0);
	if(s)
	{
		const std::map<int, Link> & allLinks = s->getLinks();
		for(std::map<int, Link>::const_iterator iter = allLinks.begin(); iter!=allLinks.end(); ++iter)
		{
			if(iter->second.type() == Link::kNeighbor ||
			   iter->second.type() == Link::kNeighborMerged)
			{
				links.insert(*iter);
			}
		}
	}
	else if(lookInDatabase && _dbDriver)
	{
		std::map<int, Link> neighbors;
		_dbDriver->loadLinks(signatureId, neighbors);
		for(std::map<int, Link>::iterator iter=neighbors.begin(); iter!=neighbors.end();)
		{
			if(iter->second.type() != Link::kNeighbor &&
			   iter->second.type() != Link::kNeighborMerged)
			{
				neighbors.erase(iter++);
			}
			else
			{
				++iter;
			}
		}
	}
	else
	{
		UWARN("Cannot find signature %d in memory", signatureId);
	}
	return links;
}

std::map<int, Link> Memory::getLoopClosureLinks(
		int signatureId,
		bool lookInDatabase) const
{
	const Signature * s = this->getSignature(signatureId);
	std::map<int, Link> loopClosures;
	if(s)
	{
		const std::map<int, Link> & allLinks = s->getLinks();
		for(std::map<int, Link>::const_iterator iter = allLinks.begin(); iter!=allLinks.end(); ++iter)
		{
			if(iter->second.type() != Link::kNeighbor &&
			   iter->second.type() != Link::kNeighborMerged &&
			   iter->second.type() != Link::kPosePrior &&
			   iter->second.type() != Link::kUndef)
			{
				loopClosures.insert(*iter);
			}
		}
	}
	else if(lookInDatabase && _dbDriver)
	{
		_dbDriver->loadLinks(signatureId, loopClosures);
		for(std::map<int, Link>::iterator iter=loopClosures.begin(); iter!=loopClosures.end();)
		{
			if(iter->second.type() == Link::kNeighbor ||
			   iter->second.type() == Link::kNeighborMerged ||
			   iter->second.type() == Link::kUndef )
			{
				loopClosures.erase(iter++);
			}
			else
			{
				++iter;
			}
		}
	}
	return loopClosures;
}

std::map<int, Link> Memory::getLinks(
		int signatureId,
		bool lookInDatabase,
		bool withLandmarks) const
{
	std::map<int, Link> links;
	if(signatureId > 0)
	{
		Signature * s = uValue(_signatures, signatureId, (Signature*)0);
		if(s)
		{
			links = s->getLinks();
			if(withLandmarks)
			{
				uInsert(links, s->getLandmarks());
			}
		}
		else if(lookInDatabase && _dbDriver)
		{
			_dbDriver->loadLinks(signatureId, links, withLandmarks?Link::kAllWithLandmarks:Link::kAllWithoutLandmarks);
		}
		else
		{
			UWARN("Cannot find signature %d in memory", signatureId);
		}
	}
	else if(signatureId < 0) //landmark
	{
		int landmarkId = signatureId;
		std::map<int, std::set<int> >::const_iterator iter = _landmarksInvertedIndex.find(landmarkId);
		if(iter != _landmarksInvertedIndex.end())
		{
			for(std::set<int>::const_iterator jter=iter->second.begin(); jter!=iter->second.end(); ++jter)
			{
				const Signature * s = getSignature(*jter);
				if(s)
				{
					std::map<int, Link>::const_iterator kter = s->getLandmarks().find(landmarkId);
					if(kter != s->getLandmarks().end())
					{
						// should be from landmark to node
						links.insert(std::make_pair(s->id(), kter->second.inverse()));
					}
				}
			}
		}
		if(_dbDriver && lookInDatabase)
		{
			std::map<int, Link> nodes;
			_dbDriver->getNodesObservingLandmark(landmarkId, nodes);
			for(std::map<int, Link>::iterator kter=nodes.begin(); kter!=nodes.end(); ++kter)
			{
				links.insert(std::make_pair(kter->first, kter->second.inverse()));
			}
		}
	}
	return links;
}

std::multimap<int, Link> Memory::getAllLinks(bool lookInDatabase, bool ignoreNullLinks, bool withLandmarks) const
{
	std::multimap<int, Link> links;

	if(lookInDatabase && _dbDriver)
	{
		_dbDriver->getAllLinks(links, ignoreNullLinks, withLandmarks);
	}

	for(std::map<int, Signature*>::const_iterator iter=_signatures.begin(); iter!=_signatures.end(); ++iter)
	{
		links.erase(iter->first);
		for(std::map<int, Link>::const_iterator jter=iter->second->getLinks().begin();
			jter!=iter->second->getLinks().end();
			++jter)
		{
			if(!ignoreNullLinks || jter->second.isValid())
			{
				links.insert(std::make_pair(iter->first, jter->second));
			}
		}
		if(withLandmarks)
		{
			for(std::map<int, Link>::const_iterator jter=iter->second->getLandmarks().begin();
				jter!=iter->second->getLandmarks().end();
				++jter)
			{
				if(!ignoreNullLinks || jter->second.isValid())
				{
					links.insert(std::make_pair(iter->first, jter->second));
				}
			}
		}
	}

	return links;
}


// return map<Id,Margin>, including signatureId
// maxCheckedInDatabase = -1 means no limit to check in database (default)
// maxCheckedInDatabase = 0 means don't check in database
std::map<int, int> Memory::getNeighborsId(
		int signatureId,
		int maxGraphDepth, // 0 means infinite margin
		int maxCheckedInDatabase, // default -1 (no limit)
		bool incrementMarginOnLoop, // default false
		bool ignoreLoopIds, // default false
		bool ignoreIntermediateNodes, // default false
		bool ignoreLocalSpaceLoopIds, // default false, ignored if ignoreLoopIds=true
		const std::set<int> & nodesSet,
		double * dbAccessTime
		) const
{
	UASSERT(maxGraphDepth >= 0);
	//UDEBUG("signatureId=%d, neighborsMargin=%d", signatureId, margin);
	if(dbAccessTime)
	{
		*dbAccessTime = 0;
	}
	std::map<int, int> ids;
	if(signatureId<=0)
	{
		return ids;
	}
	int nbLoadedFromDb = 0;
	std::list<int> curentMarginList;
	std::set<int> currentMargin;
	std::set<int> nextMargin;
	nextMargin.insert(signatureId);
	int m = 0;
	std::set<int> ignoredIds;
	while((maxGraphDepth == 0 || m < maxGraphDepth) && nextMargin.size())
	{
		// insert more recent first (priority to be loaded first from the database below if set)
		curentMarginList = std::list<int>(nextMargin.rbegin(), nextMargin.rend());
		nextMargin.clear();

		for(std::list<int>::iterator jter = curentMarginList.begin(); jter!=curentMarginList.end(); ++jter)
		{
			if(ids.find(*jter) == ids.end() && (nodesSet.empty() || nodesSet.find(*jter) != nodesSet.end()))
			{
				//UDEBUG("Added %d with margin %d", *jter, m);
				// Look up in STM/WM if all ids are here, if not... load them from the database
				const Signature * s = this->getSignature(*jter);
				std::map<int, Link> tmpLinks;
				std::map<int, Link> tmpLandmarks;
				const std::map<int, Link> * links = &tmpLinks;
				const std::map<int, Link> * landmarks = &tmpLandmarks;
				if(s)
				{
					if(!ignoreIntermediateNodes || s->getWeight() != -1)
					{
						ids.insert(std::pair<int, int>(*jter, m));
					}
					else
					{
						ignoredIds.insert(*jter);
					}

					links = &s->getLinks();
					if(!ignoreLoopIds)
					{
						landmarks = &s->getLandmarks();
					}
				}
				else if(maxCheckedInDatabase == -1 || (maxCheckedInDatabase > 0 && _dbDriver && nbLoadedFromDb < maxCheckedInDatabase))
				{
					++nbLoadedFromDb;
					ids.insert(std::pair<int, int>(*jter, m));

					UTimer timer;
					_dbDriver->loadLinks(*jter, tmpLinks, ignoreLoopIds?Link::kAllWithoutLandmarks:Link::kAllWithLandmarks);
					if(!ignoreLoopIds)
					{
						for(std::map<int, Link>::iterator kter=tmpLinks.begin(); kter!=tmpLinks.end();)
						{
							if(kter->first < 0)
							{
								tmpLandmarks.insert(*kter);
								tmpLinks.erase(kter++);
							}
							else
							{
								++kter;
							}
						}
					}
					if(dbAccessTime)
					{
						*dbAccessTime += timer.getElapsedTime();
					}
				}

				// links
				for(std::map<int, Link>::const_iterator iter=links->begin(); iter!=links->end(); ++iter)
				{
					if( !uContains(ids, iter->first) && ignoredIds.find(iter->first) == ignoredIds.end())
					{
						UASSERT(iter->second.type() != Link::kUndef);
						if(iter->second.type() == Link::kNeighbor ||
					       iter->second.type() == Link::kNeighborMerged)
						{
							if(ignoreIntermediateNodes && s->getWeight()==-1)
							{
								// stay on the same margin
								if(currentMargin.insert(iter->first).second)
								{
									curentMarginList.push_back(iter->first);
								}
							}
							else
							{
								nextMargin.insert(iter->first);
							}
						}
						else if(!ignoreLoopIds && (!ignoreLocalSpaceLoopIds || iter->second.type()!=Link::kLocalSpaceClosure))
						{
							if(incrementMarginOnLoop)
							{
								nextMargin.insert(iter->first);
							}
							else
							{
								if(currentMargin.insert(iter->first).second)
								{
									curentMarginList.push_back(iter->first);
								}
							}
						}
					}
				}

				// landmarks
				for(std::map<int, Link>::const_iterator iter=landmarks->begin(); iter!=landmarks->end(); ++iter)
				{
					const std::map<int, std::set<int> >::const_iterator kter = _landmarksInvertedIndex.find(iter->first);
					if(kter != _landmarksInvertedIndex.end())
					{
						for(std::set<int>::const_iterator nter=kter->second.begin(); nter!=kter->second.end(); ++nter)
						{
							if( !uContains(ids, *nter) && ignoredIds.find(*nter) == ignoredIds.end())
							{
								if(incrementMarginOnLoop)
								{
									nextMargin.insert(*nter);
								}
								else
								{
									if(currentMargin.insert(*nter).second)
									{
										curentMarginList.push_back(*nter);
									}
								}
							}
						}
					}
				}
			}
		}
		++m;
	}
	return ids;
}

// return map<Id,sqrdDistance>, including signatureId
std::map<int, float> Memory::getNeighborsIdRadius(
		int signatureId,
		float radius, // 0 means ignore radius
		const std::map<int, Transform> & optimizedPoses,
		int maxGraphDepth // 0 means infinite margin
		) const
{
	UASSERT(maxGraphDepth >= 0);
	UASSERT(uContains(optimizedPoses, signatureId));
	UASSERT(signatureId > 0);
	std::map<int, float> ids;
	std::map<int, float> checkedIds;
	std::list<int> curentMarginList;
	std::set<int> currentMargin;
	std::set<int> nextMargin;
	nextMargin.insert(signatureId);
	int m = 0;
	Transform referential = optimizedPoses.at(signatureId);
	UASSERT(!referential.isNull());
	float radiusSqrd = radius*radius;
	while((maxGraphDepth == 0 || m < maxGraphDepth) && nextMargin.size())
	{
		curentMarginList = std::list<int>(nextMargin.begin(), nextMargin.end());
		nextMargin.clear();

		for(std::list<int>::iterator jter = curentMarginList.begin(); jter!=curentMarginList.end(); ++jter)
		{
			if(checkedIds.find(*jter) == checkedIds.end())
			{
				//UDEBUG("Added %d with margin %d", *jter, m);
				// Look up in STM/WM if all ids are here, if not... load them from the database
				const Signature * s = this->getSignature(*jter);
				std::map<int, Link> tmpLinks;
				const std::map<int, Link> * links = &tmpLinks;
				if(s)
				{
					const Transform & t = optimizedPoses.at(*jter);
					UASSERT(!t.isNull());
					float distanceSqrd = referential.getDistanceSquared(t);
					if(radiusSqrd == 0 || distanceSqrd<radiusSqrd)
					{
						ids.insert(std::pair<int, float>(*jter,distanceSqrd));
					}

					links = &s->getLinks();
				}

				// links
				for(std::map<int, Link>::const_iterator iter=links->begin(); iter!=links->end(); ++iter)
				{
					if(!uContains(ids, iter->first) &&
						uContains(optimizedPoses, iter->first) &&
						iter->second.type()!=Link::kVirtualClosure)
					{
						nextMargin.insert(iter->first);
					}
				}
			}
		}
		++m;
	}
	return ids;
}

int Memory::getNextId()
{
	return ++_idCount;
}

int Memory::incrementMapId(std::map<int, int> * reducedIds)
{
	//don't increment if there is no location in the current map
	const Signature * s = getLastWorkingSignature();
	if(s && s->mapId() == _idMapCount)
	{
		// New session! move all signatures from the STM to WM
		while(_stMem.size())
		{
			int reducedId = 0;
			int id = *_stMem.begin();
			moveSignatureToWMFromSTM(id, &reducedId);
			if(reducedIds && reducedId > 0)
			{
				reducedIds->insert(std::make_pair(id, reducedId));
			}
		}

		return ++_idMapCount;
	}
	return _idMapCount;
}

void Memory::updateAge(int signatureId)
{
	std::map<int, double>::iterator iter=_workingMem.find(signatureId);
	if(iter!=_workingMem.end())
	{
		iter->second = UTimer::now();
	}
}

int Memory::getDatabaseMemoryUsed() const
{
	int memoryUsed = 0;
	if(_dbDriver)
	{
		memoryUsed = _dbDriver->getMemoryUsed()/(1024*1024); //Byte to MB
	}

	return memoryUsed;
}

std::string Memory::getDatabaseVersion() const
{
	std::string version = "0.0.0";
	if(_dbDriver)
	{
		version = _dbDriver->getDatabaseVersion();
	}
	return version;
}

std::string Memory::getDatabaseUrl() const
{
	std::string url = "";
	if(_dbDriver)
	{
		url = _dbDriver->getUrl();
	}
	return url;
}

double Memory::getDbSavingTime() const
{
	return _dbDriver?_dbDriver->getEmptyTrashesTime():0;
}

std::set<int> Memory::getAllSignatureIds() const
{
	std::set<int> ids;
	if(_dbDriver)
	{
		_dbDriver->getAllNodeIds(ids);
	}
	for(std::map<int, Signature*>::const_iterator iter = _signatures.begin(); iter!=_signatures.end(); ++iter)
	{
		ids.insert(iter->first);
	}
	return ids;
}

void Memory::clear()
{
	UDEBUG("");

	// empty the STM
	while(_stMem.size())
	{
		moveSignatureToWMFromSTM(*_stMem.begin());
	}
	if(_stMem.size() != 0)
	{
		ULOGGER_ERROR("_stMem must be empty here, size=%d", _stMem.size());
	}
	_stMem.clear();

	this->cleanUnusedWords();

	if(_dbDriver)
	{
		_dbDriver->emptyTrashes();
		_dbDriver->join();
	}
	if(_dbDriver)
	{
		// make sure time_enter in database is at least 1 second
		// after for the next stuf added to database
		uSleep(1500);
	}

	// Save some stats to the db, save only when the mem is not empty
	if(_dbDriver && (_stMem.size() || _workingMem.size()))
	{
		unsigned int memSize = (unsigned int)(_workingMem.size() + _stMem.size());
		if(_workingMem.size() && _workingMem.begin()->first < 0)
		{
			--memSize;
		}

		// this is only a safe check...not supposed to occur.
		UASSERT_MSG(memSize == _signatures.size(),
				uFormat("The number of signatures don't match! _workingMem=%d, _stMem=%d, _signatures=%d",
						_workingMem.size(), _stMem.size(), _signatures.size()).c_str());

		UDEBUG("Adding statistics after run...");
		if(_memoryChanged)
		{
			ParametersMap parameters = Parameters::getDefaultParameters();
			uInsert(parameters, parameters_);
			parameters.erase(Parameters::kRtabmapWorkingDirectory()); // don't save working directory as it is machine dependent
			UDEBUG("");
			_dbDriver->addInfoAfterRun(memSize,
					_lastSignature?_lastSignature->id():0,
					UProcessInfo::getMemoryUsage(),
					_dbDriver->getMemoryUsed(),
					(int)_vwd->getVisualWords().size(),
					parameters);
		}
	}
	UDEBUG("");

	//Get the tree root (parents)
	std::map<int, Signature*> mem = _signatures;
	for(std::map<int, Signature *>::iterator i=mem.begin(); i!=mem.end(); ++i)
	{
		if(i->second)
		{
			UDEBUG("deleting from the working and the short-term memory: %d", i->first);
			this->moveToTrash(i->second);
		}
	}

	if(_workingMem.size() != 0 && !(_workingMem.size() == 1 && _workingMem.begin()->first == kIdVirtual))
	{
		ULOGGER_ERROR("_workingMem must be empty here, size=%d", _workingMem.size());
	}
	_workingMem.clear();
	if(_signatures.size()!=0)
	{
		ULOGGER_ERROR("_signatures must be empty here, size=%d", _signatures.size());
	}
	_signatures.clear();

	UDEBUG("");
	// Wait until the db trash has finished cleaning the memory
	if(_dbDriver)
	{
		_dbDriver->emptyTrashes();
	}
	UDEBUG("");
	_lastSignature = 0;
	_lastGlobalLoopClosureId = 0;
	_idCount = kIdStart;
	_idMapCount = kIdStart;
	_memoryChanged = false;
	_linksChanged = false;
	_gpsOrigin = GPS();
	_rectCameraModels.clear();
	_rectStereoCameraModel = StereoCameraModel();
	_odomMaxInf.clear();
	_groundTruths.clear();
	_labels.clear();
	_landmarksIndex.clear();
	_landmarksInvertedIndex.clear();
	_allNodesInWM = true;

	if(_dbDriver)
	{
		_dbDriver->join(true);
		cleanUnusedWords();
		_dbDriver->emptyTrashes();
	}
	else
	{
		cleanUnusedWords();
	}
	if(_vwd)
	{
		_vwd->clear();
	}
	UDEBUG("");
}

/**
 * Compute the likelihood of the signature with some others in the memory.
 * Important: Assuming that all other ids are under 'signature' id.
 * If an error occurs, the result is empty.
 */
std::map<int, float> Memory::computeLikelihood(const Signature * signature, const std::list<int> & ids)
{
	if(!_tfIdfLikelihoodUsed)
	{
		UTimer timer;
		timer.start();
		std::map<int, float> likelihood;

		if(!signature)
		{
			ULOGGER_ERROR("The signature is null");
			return likelihood;
		}
		else if(ids.empty())
		{
			UWARN("ids list is empty");
			return likelihood;
		}

		for(std::list<int>::const_iterator iter = ids.begin(); iter!=ids.end(); ++iter)
		{
			float sim = 0.0f;
			if(*iter > 0)
			{
				const Signature * sB = this->getSignature(*iter);
				if(!sB)
				{
					UFATAL("Signature %d not found in WM ?!?", *iter);
				}
				sim = signature->compareTo(*sB);
			}

			likelihood.insert(likelihood.end(), std::pair<int, float>(*iter, sim));
		}

		UDEBUG("compute likelihood (similarity)... %f s", timer.ticks());
		return likelihood;
	}
	else
	{
		UTimer timer;
		timer.start();
		std::map<int, float> likelihood;
		std::map<int, float> calculatedWordsRatio;

		if(!signature)
		{
			ULOGGER_ERROR("The signature is null");
			return likelihood;
		}
		else if(ids.empty())
		{
			UWARN("ids list is empty");
			return likelihood;
		}

		for(std::list<int>::const_iterator iter = ids.begin(); iter!=ids.end(); ++iter)
		{
			likelihood.insert(likelihood.end(), std::pair<int, float>(*iter, 0.0f));
		}

		const std::list<int> & wordIds = uUniqueKeys(signature->getWords());

		float nwi; // nwi is the number of a specific word referenced by a place
		float ni; // ni is the total of words referenced by a place
		float nw; // nw is the number of places referenced by a specific word
		float N; // N is the total number of places

		float logNnw;
		const VisualWord * vw;

		N = this->getSignatures().size();

		if(N)
		{
			UDEBUG("processing... ");
			// Pour chaque mot dans la signature SURF
			for(std::list<int>::const_iterator i=wordIds.begin(); i!=wordIds.end(); ++i)
			{
				if(*i>0)
				{
					// "Inverted index" - Pour chaque endroit contenu dans chaque mot
					vw = _vwd->getWord(*i);
					UASSERT_MSG(vw!=0, uFormat("Word %d not found in dictionary!?", *i).c_str());

					const std::map<int, int> & refs = vw->getReferences();
					nw = refs.size();
					if(nw)
					{
						logNnw = log10(N/nw);
						if(logNnw)
						{
							for(std::map<int, int>::const_iterator j=refs.begin(); j!=refs.end(); ++j)
							{
								std::map<int, float>::iterator iter = likelihood.find(j->first);
								if(iter != likelihood.end())
								{
									nwi = j->second;
									ni = this->getNi(j->first);
									if(ni != 0)
									{
										//UDEBUG("%d, %f %f %f %f", vw->id(), logNnw, nwi, ni, ( nwi  * logNnw ) / ni);
										iter->second += ( nwi  * logNnw ) / ni;
									}
								}
							}
						}
					}
				}
			}
		}

		UDEBUG("compute likelihood (tf-idf) %f s", timer.ticks());
		return likelihood;
	}
}

// Weights of the signatures in the working memory <signature id, weight>
std::map<int, int> Memory::getWeights() const
{
	std::map<int, int> weights;
	for(std::map<int, double>::const_iterator iter=_workingMem.begin(); iter!=_workingMem.end(); ++iter)
	{
		if(iter->first > 0)
		{
			const Signature * s = this->getSignature(iter->first);
			if(!s)
			{
				UFATAL("Location %d must exist in memory", iter->first);
			}
			weights.insert(weights.end(), std::make_pair(iter->first, s->getWeight()));
		}
		else
		{
			weights.insert(weights.end(), std::make_pair(iter->first, -1));
		}
	}
	return weights;
}

std::list<int> Memory::forget(const std::set<int> & ignoredIds)
{
	UDEBUG("");
	std::list<int> signaturesRemoved;
	if(this->isIncremental() &&
	   _vwd->isIncremental() &&
	   _vwd->getVisualWords().size() &&
	   !_vwd->isIncrementalFlann())
	{
		// Note that when using incremental FLANN, the number of words
		// is not the biggest issue, so use the number of signatures instead
		// of the number of words

		int newWords = 0;
		int wordsRemoved = 0;

		// Get how many new words added for the last run...
		newWords = _vwd->getNotIndexedWordsCount();

		// So we need to remove at least "newWords" words from the
		// dictionary to respect the limit.
		while(wordsRemoved < newWords)
		{
			std::list<Signature *> signatures = this->getRemovableSignatures(1, ignoredIds);
			if(signatures.size())
			{
				Signature *  s = dynamic_cast<Signature *>(signatures.front());
				if(s)
				{
					signaturesRemoved.push_back(s->id());
					this->moveToTrash(s);
					wordsRemoved = _vwd->getUnusedWordsSize();
				}
				else
				{
					break;
				}
			}
			else
			{
				break;
			}
		}
		UDEBUG("newWords=%d, wordsRemoved=%d", newWords, wordsRemoved);
	}
	else
	{
		UDEBUG("");
		// Remove one more than total added during the iteration
		int signaturesAdded = _signaturesAdded;
		std::list<Signature *> signatures = getRemovableSignatures(signaturesAdded+1, ignoredIds);
		for(std::list<Signature *>::iterator iter=signatures.begin(); iter!=signatures.end(); ++iter)
		{
			signaturesRemoved.push_back((*iter)->id());
			// When a signature is deleted, it notifies the memory
			// and it is removed from the memory list
			this->moveToTrash(*iter);
		}
		if((int)signatures.size() < signaturesAdded)
		{
			UWARN("Less signatures transferred (%d) than added (%d)! The working memory cannot decrease in size.",
					(int)signatures.size(), signaturesAdded);
		}
		else
		{
			UDEBUG("signaturesRemoved=%d, _signaturesAdded=%d", (int)signatures.size(), signaturesAdded);
		}
	}
	return signaturesRemoved;
}


int Memory::cleanup()
{
	UDEBUG("");
	int signatureRemoved = 0;

	// bad signature
	if(_lastSignature && ((_lastSignature->isBadSignature() && _badSignaturesIgnored) || !_incrementalMemory))
	{
		if(_lastSignature->isBadSignature())
		{
			UDEBUG("Bad signature! %d", _lastSignature->id());
		}
		signatureRemoved = _lastSignature->id();
		moveToTrash(_lastSignature, _incrementalMemory);
	}

	return signatureRemoved;
}

void Memory::saveStatistics(const Statistics & statistics)
{
	if(_dbDriver)
	{
		_dbDriver->addStatistics(statistics);
	}
}

void Memory::savePreviewImage(const cv::Mat & image) const
{
	if(_dbDriver)
	{
		_dbDriver->savePreviewImage(image);
	}
}
cv::Mat Memory::loadPreviewImage() const
{
	if(_dbDriver)
	{
		return _dbDriver->loadPreviewImage();
	}
	return cv::Mat();
}

void Memory::saveOptimizedPoses(const std::map<int, Transform> & optimizedPoses, const Transform & lastlocalizationPose) const
{
	if(_dbDriver)
	{
		_dbDriver->saveOptimizedPoses(optimizedPoses, lastlocalizationPose);
	}
}

std::map<int, Transform> Memory::loadOptimizedPoses(Transform * lastlocalizationPose) const
{
	if(_dbDriver)
	{
		return _dbDriver->loadOptimizedPoses(lastlocalizationPose);
	}
	return std::map<int, Transform>();
}

void Memory::save2DMap(const cv::Mat & map, float xMin, float yMin, float cellSize) const
{
	if(_dbDriver)
	{
		_dbDriver->save2DMap(map, xMin, yMin, cellSize);
	}
}

cv::Mat Memory::load2DMap(float & xMin, float & yMin, float & cellSize) const
{
	if(_dbDriver)
	{
		return _dbDriver->load2DMap(xMin, yMin, cellSize);
	}
	return cv::Mat();
}

void Memory::saveOptimizedMesh(
		const cv::Mat & cloud,
		const std::vector<std::vector<std::vector<unsigned int> > > & polygons,
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
		const std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > & texCoords,
#else
		const std::vector<std::vector<Eigen::Vector2f> > & texCoords,
#endif
		const cv::Mat & textures) const
{
	if(_dbDriver)
	{
		_dbDriver->saveOptimizedMesh(cloud, polygons, texCoords, textures);
	}
}

cv::Mat Memory::loadOptimizedMesh(
			std::vector<std::vector<std::vector<unsigned int> > > * polygons,
#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
			std::vector<std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > > * texCoords,
#else
			std::vector<std::vector<Eigen::Vector2f> > * texCoords,
#endif
			cv::Mat * textures) const
{
	if(_dbDriver)
	{
		return _dbDriver->loadOptimizedMesh(polygons, texCoords, textures);
	}
	return cv::Mat();
}

void Memory::emptyTrash()
{
	if(_dbDriver)
	{
		_dbDriver->emptyTrashes(true);
	}
}

void Memory::joinTrashThread()
{
	if(_dbDriver)
	{
		UDEBUG("");
		_dbDriver->join();
		UDEBUG("");
	}
}

class WeightAgeIdKey
{
public:
	WeightAgeIdKey(int w, double a, int i) :
		weight(w),
		age(a),
		id(i){}
	bool operator<(const WeightAgeIdKey & k) const
	{
		if(weight < k.weight)
		{
			return true;
		}
		else if(weight == k.weight)
		{
			if(age < k.age)
			{
				return true;
			}
			else if(age == k.age)
			{
				if(id < k.id)
				{
					return true;
				}
			}
		}
		return false;
	}
	int weight, age, id;
};
std::list<Signature *> Memory::getRemovableSignatures(int count, const std::set<int> & ignoredIds)
{
	//UDEBUG("");
	std::list<Signature *> removableSignatures;
	std::map<WeightAgeIdKey, Signature *> weightAgeIdMap;

	// Find the last index to check...
	UDEBUG("mem.size()=%d, ignoredIds.size()=%d", (int)_workingMem.size(), (int)ignoredIds.size());

	if(_workingMem.size())
	{
		int recentWmMaxSize = _recentWmRatio * float(_workingMem.size());
		bool recentWmImmunized = false;
		// look for the position of the lastLoopClosureId in WM
		int currentRecentWmSize = 0;
		if(_lastGlobalLoopClosureId > 0 && _stMem.find(_lastGlobalLoopClosureId) == _stMem.end())
		{
			// If set, it must be in WM
			std::map<int, double>::const_iterator iter = _workingMem.find(_lastGlobalLoopClosureId);
			while(iter != _workingMem.end())
			{
				++currentRecentWmSize;
				++iter;
			}
			if(currentRecentWmSize>1 && currentRecentWmSize < recentWmMaxSize)
			{
				recentWmImmunized = true;
			}
			else if(currentRecentWmSize == 0 && _workingMem.size() > 1)
			{
				UERROR("Last loop closure id not found in WM (%d)", _lastGlobalLoopClosureId);
			}
			UDEBUG("currentRecentWmSize=%d, recentWmMaxSize=%d, _recentWmRatio=%f, end recent wM = %d", currentRecentWmSize, recentWmMaxSize, _recentWmRatio, _lastGlobalLoopClosureId);
		}

		// Ignore neighbor of the last location in STM (for neighbor links redirection issue during Rehearsal).
		Signature * lastInSTM = 0;
		if(_stMem.size())
		{
			lastInSTM = _signatures.at(*_stMem.begin());
		}

		for(std::map<int, double>::const_iterator memIter = _workingMem.begin(); memIter != _workingMem.end(); ++memIter)
		{
			if( (recentWmImmunized && memIter->first > _lastGlobalLoopClosureId) ||
				memIter->first == _lastGlobalLoopClosureId)
			{
				// ignore recent memory
			}
			else if(memIter->first > 0 && ignoredIds.find(memIter->first) == ignoredIds.end() && (!lastInSTM || !lastInSTM->hasLink(memIter->first)))
			{
				Signature * s = this->_getSignature(memIter->first);
				if(s)
				{
					// Links must not be in STM to be removable, rehearsal issue
					bool foundInSTM = false;
					for(std::map<int, Link>::const_iterator iter = s->getLinks().begin(); iter!=s->getLinks().end(); ++iter)
					{
						if(_stMem.find(iter->first) != _stMem.end())
						{
							UDEBUG("Ignored %d because it has a link (%d) to STM", s->id(), iter->first);
							foundInSTM = true;
							break;
						}
					}
					if(!foundInSTM)
					{
						// less weighted signature priority to be transferred
						weightAgeIdMap.insert(std::make_pair(WeightAgeIdKey(s->getWeight(), _transferSortingByWeightId?0.0:memIter->second, s->id()), s));
					}
				}
				else
				{
					ULOGGER_ERROR("Not supposed to occur!!!");
				}
			}
			else
			{
				//UDEBUG("Ignoring id %d", memIter->first);
			}
		}

		int recentWmCount = 0;
		// make the list of removable signatures
		// Criteria : Weight -> ID
		UDEBUG("signatureMap.size()=%d _lastGlobalLoopClosureId=%d currentRecentWmSize=%d recentWmMaxSize=%d",
				(int)weightAgeIdMap.size(), _lastGlobalLoopClosureId, currentRecentWmSize, recentWmMaxSize);
		for(std::map<WeightAgeIdKey, Signature*>::iterator iter=weightAgeIdMap.begin();
			iter!=weightAgeIdMap.end();
			++iter)
		{
			if(!recentWmImmunized)
			{
				UDEBUG("weight=%d, id=%d",
						iter->second->getWeight(),
						iter->second->id());
				removableSignatures.push_back(iter->second);

				if(_lastGlobalLoopClosureId && iter->second->id() > _lastGlobalLoopClosureId)
				{
					++recentWmCount;
					if(currentRecentWmSize - recentWmCount < recentWmMaxSize)
					{
						UDEBUG("switched recentWmImmunized");
						recentWmImmunized = true;
					}
				}
			}
			else if(_lastGlobalLoopClosureId == 0 || iter->second->id() < _lastGlobalLoopClosureId)
			{
				UDEBUG("weight=%d, id=%d",
						iter->second->getWeight(),
						iter->second->id());
				removableSignatures.push_back(iter->second);
			}
			if(removableSignatures.size() >= (unsigned int)count)
			{
				break;
			}
		}
	}
	else
	{
		ULOGGER_WARN("not enough signatures to get an old one...");
	}
	return removableSignatures;
}

/**
 * If saveToDatabase=false, deleted words are filled in deletedWords.
 */
void Memory::moveToTrash(Signature * s, bool keepLinkedToGraph, std::list<int> * deletedWords)
{
	UDEBUG("id=%d", s?s->id():0);
	if(s)
	{
		// Cleanup landmark indexes
		if(!s->getLandmarks().empty())
		{
			for(std::map<int, Link>::const_iterator iter=s->getLandmarks().begin(); iter!=s->getLandmarks().end(); ++iter)
			{
				int landmarkId = iter->first;
				std::map<int, std::set<int> >::iterator nter = _landmarksIndex.find(s->id());
				if(nter!=_landmarksIndex.end())
				{
					nter->second.erase(landmarkId);
					if(nter->second.empty())
					{
						_landmarksIndex.erase(nter);
					}
				}
				nter = _landmarksInvertedIndex.find(landmarkId);
				if(nter!=_landmarksInvertedIndex.end())
				{
					nter->second.erase(s->id());
					if(nter->second.empty())
					{
						_landmarksInvertedIndex.erase(nter);
					}
				}
			}
		}

		// it is a bad signature (not saved), remove links!
		if(keepLinkedToGraph && (!s->isSaved() && s->isBadSignature() && _badSignaturesIgnored))
		{
			keepLinkedToGraph = false;
		}

		// If not saved to database
		if(!keepLinkedToGraph)
		{
			UASSERT_MSG(this->isInSTM(s->id()),
						uFormat("Deleting location (%d) outside the "
								"STM is not implemented!", s->id()).c_str());
			const std::map<int, Link> & links = s->getLinks();
			for(std::map<int, Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
			{
				if(iter->second.from() != iter->second.to() && iter->first > 0)
				{
					Signature * sTo = this->_getSignature(iter->first);
					// neighbor to s
					UASSERT_MSG(sTo!=0,
								uFormat("A neighbor (%d) of the deleted location %d is "
										"not found in WM/STM! Are you deleting a location "
										"outside the STM?", iter->first, s->id()).c_str());

					if(iter->first > s->id() && links.size()>1 && sTo->hasLink(s->id()))
					{
						UWARN("Link %d of %d is newer, removing neighbor link "
							  "may split the map!",
								iter->first, s->id());
					}

					// child
					if(iter->second.type() == Link::kGlobalClosure && s->id() > sTo->id())
					{
						sTo->setWeight(sTo->getWeight() + s->getWeight()); // copy weight
					}

					sTo->removeLink(s->id());
				}

			}
			s->removeLinks(); // remove all links
			s->setWeight(0);
			s->setLabel(""); // reset label
		}
		else
		{
			// Make sure that virtual links are removed.
			// It should be called before the signature is
			// removed from _signatures below.
			removeVirtualLinks(s->id());
		}

		this->disableWordsRef(s->id());
		if(!keepLinkedToGraph && _vwd->isIncremental())
		{
			std::list<int> keys = uUniqueKeys(s->getWords());
			for(std::list<int>::const_iterator i=keys.begin(); i!=keys.end(); ++i)
			{
				// assume just removed word doesn't have any other references
				VisualWord * w = _vwd->getUnusedWord(*i);
				if(w)
				{
					std::vector<VisualWord*> wordToDelete;
					wordToDelete.push_back(w);
					_vwd->removeWords(wordToDelete);
					if(deletedWords)
					{
						deletedWords->push_back(w->id());
					}
					delete w;
				}
			}
		}

		_workingMem.erase(s->id());
		_stMem.erase(s->id());
		_signatures.erase(s->id());
		_groundTruths.erase(s->id());
		if(_signaturesAdded>0)
		{
			--_signaturesAdded;
		}

		if(_lastSignature == s)
		{
			_lastSignature = 0;
			if(_stMem.size())
			{
				_lastSignature = this->_getSignature(*_stMem.rbegin());
			}
			else if(_workingMem.size())
			{
				_lastSignature = this->_getSignature(_workingMem.rbegin()->first);
			}
		}

		if(_lastGlobalLoopClosureId == s->id())
		{
			_lastGlobalLoopClosureId = 0;
		}

		if(	(_notLinkedNodesKeptInDb || keepLinkedToGraph || s->isSaved()) &&
			_dbDriver &&
			s->id()>0 &&
			(_incrementalMemory || s->isSaved()))
		{
			if(keepLinkedToGraph)
			{
				_allNodesInWM = false;
			}
			_dbDriver->asyncSave(s);
		}
		else
		{
			delete s;
		}
	}
}

int Memory::getLastSignatureId() const
{
	return _idCount;
}

const Signature * Memory::getLastWorkingSignature() const
{
	UDEBUG("");
	return _lastSignature;
}

std::map<int, Link> Memory::getNodesObservingLandmark(int landmarkId, bool lookInDatabase) const
{
	UDEBUG("landmarkId=%d", landmarkId);
	std::map<int, Link> nodes;
	if(landmarkId < 0)
	{
		std::map<int, std::set<int> >::const_iterator iter = _landmarksInvertedIndex.find(landmarkId);
		if(iter != _landmarksInvertedIndex.end())
		{
			for(std::set<int>::const_iterator jter=iter->second.begin(); jter!=iter->second.end(); ++jter)
			{
				const Signature * s = getSignature(*jter);
				if(s)
				{
					std::map<int, Link>::const_iterator kter = s->getLandmarks().find(landmarkId);
					if(kter != s->getLandmarks().end())
					{
						nodes.insert(std::make_pair(s->id(), kter->second));
					}
				}
			}
		}
		if(_dbDriver && lookInDatabase)
		{
			_dbDriver->getNodesObservingLandmark(landmarkId, nodes);
		}
	}
	return nodes;
}

int Memory::getSignatureIdByLabel(const std::string & label, bool lookInDatabase) const
{
	UDEBUG("label=%s", label.c_str());
	int id = 0;
	if(label.size())
	{
		for(std::map<int, Signature*>::const_iterator iter=_signatures.begin(); iter!=_signatures.end(); ++iter)
		{
			UASSERT(iter->second != 0);
			if(iter->second->getLabel().compare(label) == 0)
			{
				id = iter->second->id();
				break;
			}
		}
		if(id == 0 && _dbDriver && lookInDatabase)
		{
			_dbDriver->getNodeIdByLabel(label, id);
		}
	}
	return id;
}

bool Memory::labelSignature(int id, const std::string & label)
{
	// verify that this label is not used
	int idFound=getSignatureIdByLabel(label);
	if(idFound == 0 || idFound == id)
	{
		Signature * s  = this->_getSignature(id);
		if(s)
		{
			uInsert(_labels, std::make_pair(s->id(), label));
			s->setLabel(label);
			_linksChanged = s->isSaved(); // HACK to get label updated in Localization mode
			UWARN("Label \"%s\" set to node %d", label.c_str(), id);
			return true;
		}
		else if(_dbDriver)
		{
			std::list<int> ids;
			ids.push_back(id);
			std::list<Signature *> signatures;
			_dbDriver->loadSignatures(ids,signatures);
			if(signatures.size())
			{
				uInsert(_labels, std::make_pair(signatures.front()->id(), label));
				signatures.front()->setLabel(label);
				UWARN("Label \"%s\" set to node %d", label.c_str(), id);
				_dbDriver->asyncSave(signatures.front()); // move it again to trash
				return true;
			}
		}
		else
		{
			UERROR("Node %d not found, failed to set label \"%s\"!", id, label.c_str());
		}
	}
	else if(idFound)
	{
		UWARN("Node %d has already label \"%s\"", idFound, label.c_str());
	}
	return false;
}

bool Memory::setUserData(int id, const cv::Mat & data)
{
	Signature * s  = this->_getSignature(id);
	if(s)
	{
		s->sensorData().setUserData(data);
		return true;
	}
	else
	{
		UERROR("Node %d not found in RAM, failed to set user data (size=%d)!", id, data.total());
	}
	return false;
}

void Memory::deleteLocation(int locationId, std::list<int> * deletedWords)
{
	UDEBUG("Deleting location %d", locationId);
	Signature * location = _getSignature(locationId);
	if(location)
	{
		this->moveToTrash(location, false, deletedWords);
	}
}

void Memory::saveLocationData(int locationId)
{
	UDEBUG("Saving location data %d", locationId);
	Signature * location = _getSignature(locationId);
	if( location &&
		_dbDriver &&
		!_dbDriver->isInMemory() && // don't push in database if it is also in memory.
		location->id()>0 &&
		(_incrementalMemory && !location->isSaved()))
	{
		Signature * cpy = new Signature();
		*cpy = *location;
		_dbDriver->asyncSave(cpy);

		location->setSaved(true);
		location->sensorData().clearCompressedData();
	}
}

void Memory::removeLink(int oldId, int newId)
{
	//this method assumes receiving oldId < newId, if not switch them
	Signature * oldS = this->_getSignature(oldId<newId?oldId:newId);
	Signature * newS = this->_getSignature(oldId<newId?newId:oldId);
	if(oldS && newS)
	{
		UINFO("removing link between location %d and %d", oldS->id(), newS->id());

		if(oldS->hasLink(newS->id()) && newS->hasLink(oldS->id()))
		{
			Link::Type type = oldS->getLinks().at(newS->id()).type();
			if(type == Link::kGlobalClosure && newS->getWeight() > 0)
			{
				// adjust the weight
				oldS->setWeight(oldS->getWeight()+1);
				newS->setWeight(newS->getWeight()>0?newS->getWeight()-1:0);
			}


			oldS->removeLink(newS->id());
			newS->removeLink(oldS->id());

			if(type!=Link::kVirtualClosure)
			{
				_linksChanged = true;
			}

			bool noChildrenAnymore = true;
			for(std::map<int, Link>::const_iterator iter=newS->getLinks().begin(); iter!=newS->getLinks().end(); ++iter)
			{
				if(iter->second.type() != Link::kNeighbor &&
				   iter->second.type() != Link::kNeighborMerged &&
				   iter->second.type() != Link::kPosePrior &&
				   iter->first < newS->id())
				{
					noChildrenAnymore = false;
					break;
				}
			}
			if(noChildrenAnymore && newS->id() == _lastGlobalLoopClosureId)
			{
				_lastGlobalLoopClosureId = 0;
			}
		}
		else
		{
			UERROR("Signatures %d and %d don't have bidirectional link!", oldS->id(), newS->id());
		}
	}
	else
	{
		if(!newS)
		{
			UERROR("Signature %d is not in working memory... cannot remove link.", newS->id());
		}
		if(!oldS)
		{
			UERROR("Signature %d is not in working memory... cannot remove link.", oldS->id());
		}
	}
}

void Memory::removeRawData(int id, bool image, bool scan, bool userData)
{
	UDEBUG("id=%d image=%d scan=%d userData=%d", id, image?1:0, scan?1:0, userData?1:0);
	Signature * s = this->_getSignature(id);
	if(s)
	{
		s->sensorData().clearRawData(
				image && (!_reextractLoopClosureFeatures || !_registrationPipeline->isImageRequired()),
				scan && !_registrationPipeline->isScanRequired(),
				userData && !_registrationPipeline->isUserDataRequired());
	}
}

// compute transform fromId -> toId
Transform Memory::computeTransform(
		int fromId,
		int toId,
		Transform guess,
		RegistrationInfo * info,
		bool useKnownCorrespondencesIfPossible)
{
	Signature * fromS = this->_getSignature(fromId);
	Signature * toS = this->_getSignature(toId);

	Transform transform;

	if(fromS && toS)
	{
		transform = computeTransform(*fromS, *toS, guess, info, useKnownCorrespondencesIfPossible);
	}
	else
	{
		std::string msg = uFormat("Did not find nodes %d and/or %d", fromId, toId);
		if(info)
		{
			info->rejectedMsg = msg;
		}
		UWARN(msg.c_str());
	}
	return transform;
}

// compute transform fromId -> toId
Transform Memory::computeTransform(
		Signature & fromS,
		Signature & toS,
		Transform guess,
		RegistrationInfo * info,
		bool useKnownCorrespondencesIfPossible) const
{
	UDEBUG("");
	Transform transform;

	// make sure we have all data needed
	// load binary data from database if not in RAM (if image is already here, scan and userData should be or they are null)
	if(((_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired()) && fromS.sensorData().imageCompressed().empty()) ||
	   (_registrationPipeline->isScanRequired() && fromS.sensorData().imageCompressed().empty() && fromS.sensorData().laserScanCompressed().isEmpty()) ||
	   (_registrationPipeline->isUserDataRequired() && fromS.sensorData().imageCompressed().empty() && fromS.sensorData().userDataCompressed().empty()))
	{
		fromS.sensorData() = getNodeData(fromS.id());
	}
	if(((_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired()) && toS.sensorData().imageCompressed().empty()) ||
	   (_registrationPipeline->isScanRequired() && toS.sensorData().imageCompressed().empty() && toS.sensorData().laserScanCompressed().isEmpty()) ||
	   (_registrationPipeline->isUserDataRequired() && toS.sensorData().imageCompressed().empty() && toS.sensorData().userDataCompressed().empty()))
	{
		toS.sensorData() = getNodeData(toS.id());
	}
	// uncompress only what we need
	cv::Mat imgBuf, depthBuf, userBuf;
	LaserScan laserBuf;
	fromS.sensorData().uncompressData(
			(_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired())?&imgBuf:0,
			(_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired())?&depthBuf:0,
			_registrationPipeline->isScanRequired()?&laserBuf:0,
			_registrationPipeline->isUserDataRequired()?&userBuf:0);
	toS.sensorData().uncompressData(
			(_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired())?&imgBuf:0,
			(_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired())?&depthBuf:0,
			_registrationPipeline->isScanRequired()?&laserBuf:0,
			_registrationPipeline->isUserDataRequired()?&userBuf:0);


	// compute transform fromId -> toId
	std::vector<int> inliersV;
	if((_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired()) ||
		(fromS.getWords().size() && toS.getWords().size()) ||
		(!guess.isNull() && !_registrationPipeline->isImageRequired()))
	{
		Signature tmpFrom = fromS;
		Signature tmpTo = toS;

		if(_reextractLoopClosureFeatures && _registrationPipeline->isImageRequired())
		{
			UDEBUG("");
			tmpFrom.setWords(std::multimap<int, cv::KeyPoint>());
			tmpFrom.setWords3(std::multimap<int, cv::Point3f>());
			tmpFrom.setWordsDescriptors(std::multimap<int, cv::Mat>());
			tmpFrom.sensorData().setFeatures(std::vector<cv::KeyPoint>(), std::vector<cv::Point3f>(), cv::Mat());
			tmpTo.setWords(std::multimap<int, cv::KeyPoint>());
			tmpTo.setWords3(std::multimap<int, cv::Point3f>());
			tmpTo.setWordsDescriptors(std::multimap<int, cv::Mat>());
			tmpTo.sensorData().setFeatures(std::vector<cv::KeyPoint>(), std::vector<cv::Point3f>(), cv::Mat());
		}
		else if(useKnownCorrespondencesIfPossible)
		{
			// This will make RegistrationVis bypassing the correspondences computation
			tmpFrom.setWordsDescriptors(std::multimap<int, cv::Mat>());
			tmpTo.setWordsDescriptors(std::multimap<int, cv::Mat>());
		}

		bool isNeighborRefining = fromS.getLinks().find(toS.id()) != fromS.getLinks().end() && fromS.getLinks().find(toS.id())->second.type() == Link::kNeighbor;

		if(guess.isNull() && !_registrationPipeline->isImageRequired())
		{
			UDEBUG("");
			// no visual in the pipeline, make visual registration for guess
			// make sure feature matching is used instead of optical flow to compute the guess
			ParametersMap parameters = parameters_;
			uInsert(parameters, ParametersPair(Parameters::kVisCorType(), "0"));
			uInsert(parameters, ParametersPair(Parameters::kRegRepeatOnce(), "false"));
			RegistrationVis regVis(parameters);
			guess = regVis.computeTransformation(tmpFrom, tmpTo, guess, info);
			if(!guess.isNull())
			{
				transform = _registrationPipeline->computeTransformationMod(tmpFrom, tmpTo, guess, info);
			}
		}
		else if(!isNeighborRefining &&
				_localBundleOnLoopClosure &&
				_registrationPipeline->isImageRequired() &&
			   !_registrationPipeline->isScanRequired() &&
			   !_registrationPipeline->isUserDataRequired() &&
			   !tmpTo.getWordsDescriptors().empty() &&
			   !tmpTo.getWords().empty() &&
			   !tmpFrom.getWordsDescriptors().empty() &&
			   !tmpFrom.getWords().empty() &&
			   !tmpFrom.getWords3().empty())
		{
			std::multimap<int, cv::Point3f> words3DMap;
			std::multimap<int, cv::KeyPoint> wordsMap;
			std::multimap<int, cv::Mat> wordsDescriptorsMap;

			const std::map<int, Link> & links = fromS.getLinks();
			{
				const std::map<int, cv::Point3f> & words3 = uMultimapToMapUnique(fromS.getWords3());
				UDEBUG("fromS.getWords3()=%d  uniques=%d", (int)fromS.getWords3().size(), (int)words3.size());
				for(std::map<int, cv::Point3f>::const_iterator jter=words3.begin(); jter!=words3.end(); ++jter)
				{
					if(util3d::isFinite(jter->second))
					{
						words3DMap.insert(*jter);
						wordsMap.insert(*fromS.getWords().find(jter->first));
						wordsDescriptorsMap.insert(*fromS.getWordsDescriptors().find(jter->first));
					}
				}
			}
			UDEBUG("words3DMap=%d", (int)words3DMap.size());

			for(std::map<int, Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
			{
				int id = iter->first;
				const Signature * s = this->getSignature(id);
				if(s)
				{
					const std::map<int, cv::Point3f> & words3 = uMultimapToMapUnique(s->getWords3());
					for(std::map<int, cv::Point3f>::const_iterator jter=words3.begin(); jter!=words3.end(); ++jter)
					{
						if( jter->first > 0 &&
							util3d::isFinite(jter->second) &&
							words3DMap.find(jter->first) == words3DMap.end())
						{
							words3DMap.insert(std::make_pair(jter->first, util3d::transformPoint(jter->second, iter->second.transform())));
							wordsMap.insert(*s->getWords().find(jter->first));
							wordsDescriptorsMap.insert(*s->getWordsDescriptors().find(jter->first));
						}
					}
				}
			}
			UDEBUG("words3DMap=%d", (int)words3DMap.size());
			Signature tmpFrom2(fromS.id());
			tmpFrom2.setWords3(words3DMap);
			tmpFrom2.setWords(wordsMap);
			tmpFrom2.setWordsDescriptors(wordsDescriptorsMap);

			transform = _registrationPipeline->computeTransformationMod(tmpFrom2, tmpTo, guess, info);

			if(!transform.isNull() && info)
			{
				std::map<int, cv::Point3f> points3DMap = uMultimapToMapUnique(tmpFrom2.getWords3());
				std::map<int, Transform> bundlePoses;
				std::multimap<int, Link> bundleLinks;
				std::map<int, CameraModel> bundleModels;
				std::map<int, std::map<int, FeatureBA> > wordReferences;

				std::map<int, Link> links = fromS.getLinks();
				links.insert(std::make_pair(toS.id(), Link(fromS.id(), toS.id(), Link::kGlobalClosure, transform, info->covariance.inv())));
				links.insert(std::make_pair(fromS.id(), Link()));

				for(std::map<int, Link>::iterator iter=links.begin(); iter!=links.end(); ++iter)
				{
					int id = iter->first;
					const Signature * s;
					if(id == tmpTo.id())
					{
						s = &tmpTo; // reuse matched words
					}
					else
					{
						s = this->getSignature(id);
					}
					if(s)
					{
						CameraModel model;
						if(s->sensorData().cameraModels().size() == 1 && s->sensorData().cameraModels().at(0).isValidForProjection())
						{
							model = s->sensorData().cameraModels()[0];
						}
						else if(s->sensorData().stereoCameraModel().isValidForProjection())
						{
							model = s->sensorData().stereoCameraModel().left();
							// Set Tx for stereo BA
							model = CameraModel(model.fx(),
									model.fy(),
									model.cx(),
									model.cy(),
									model.localTransform(),
									-s->sensorData().stereoCameraModel().baseline()*model.fx());
						}
						else
						{
							UFATAL("no valid camera model to use local bundle adjustment on loop closure!");
						}
						bundleModels.insert(std::make_pair(id, model));
						Transform invLocalTransform = model.localTransform().inverse();
						if(iter->second.isValid())
						{
							bundleLinks.insert(std::make_pair(iter->second.from(), iter->second));
							bundlePoses.insert(std::make_pair(id, iter->second.transform()));
						}
						else
						{
							bundlePoses.insert(std::make_pair(id, Transform::getIdentity()));
						}
						const std::map<int,cv::KeyPoint> & words = uMultimapToMapUnique(s->getWords());
						for(std::map<int, cv::KeyPoint>::const_iterator jter=words.begin(); jter!=words.end(); ++jter)
						{
							if(points3DMap.find(jter->first)!=points3DMap.end() &&
								(id == tmpTo.id() || jter->first > 0))
							{
								std::multimap<int, cv::Point3f>::const_iterator kter = s->getWords3().find(jter->first);
								cv::Point3f pt3d = util3d::transformPoint(kter->second, invLocalTransform);
								wordReferences.insert(std::make_pair(jter->first, std::map<int, FeatureBA>()));
								wordReferences.at(jter->first).insert(std::make_pair(id, FeatureBA(jter->second, pt3d.z)));
							}
						}
					}
				}

				UDEBUG("sba...start");
				// set root negative to fix all other poses
				std::set<int> sbaOutliers;
				UTimer bundleTimer;
				OptimizerG2O sba;
				UTimer bundleTime;
				bundlePoses = sba.optimizeBA(-toS.id(), bundlePoses, bundleLinks, bundleModels, points3DMap, wordReferences, &sbaOutliers);
				UDEBUG("sba...end");

				UDEBUG("bundleTime=%fs (poses=%d wordRef=%d outliers=%d)", bundleTime.ticks(), (int)bundlePoses.size(), (int)wordReferences.size(), (int)sbaOutliers.size());

				UDEBUG("Local Bundle Adjustment Before: %s", transform.prettyPrint().c_str());
				if(!bundlePoses.rbegin()->second.isNull())
				{
					if(sbaOutliers.size())
					{
						std::vector<int> newInliers(info->inliersIDs.size());
						int oi=0;
						for(unsigned int i=0; i<info->inliersIDs.size(); ++i)
						{
							if(sbaOutliers.find(info->inliersIDs[i]) == sbaOutliers.end())
							{
								newInliers[oi++] = info->inliersIDs[i];
							}
						}
						newInliers.resize(oi);
						UDEBUG("BA outliers ratio %f", float(sbaOutliers.size())/float(info->inliersIDs.size()));
						info->inliers = (int)newInliers.size();
						info->inliersIDs = newInliers;
					}
					if(info->inliers < _registrationPipeline->getMinVisualCorrespondences())
					{
						info->rejectedMsg = uFormat("Too low inliers after bundle adjustment: %d<%d", info->inliers, _registrationPipeline->getMinVisualCorrespondences());
						transform.setNull();
					}
					else
					{
						transform = bundlePoses.rbegin()->second;
						if(_registrationPipeline->force3DoF())
						{
							transform = transform.to3DoF();
						}
					}
				}
				UDEBUG("Local Bundle Adjustment After : %s", transform.prettyPrint().c_str());
			}
		}
		else
		{
			transform = _registrationPipeline->computeTransformationMod(tmpFrom, tmpTo, guess, info);
		}

		if(!transform.isNull())
		{
			UDEBUG("");
			// verify if it is a 180 degree transform, well verify > 90
			float x,y,z, roll,pitch,yaw;
			if(guess.isNull())
			{
				transform.getTranslationAndEulerAngles(x,y,z, roll,pitch,yaw);
			}
			else
			{
				Transform guessError = guess.inverse() * transform;
				guessError.getTranslationAndEulerAngles(x,y,z, roll,pitch,yaw);
			}
			if(fabs(pitch) > CV_PI/2 ||
			   fabs(yaw) > CV_PI/2)
			{
				transform.setNull();
				std::string msg = uFormat("Too large rotation detected! (pitch=%f, yaw=%f) max is %f",
						roll, pitch, yaw, CV_PI/2);
				UINFO(msg.c_str());
				if(info)
				{
					info->rejectedMsg = msg;
				}
			}
		}
	}
	return transform;
}

// compute transform fromId -> multiple toId
Transform Memory::computeIcpTransformMulti(
		int fromId,
		int toId,
		const std::map<int, Transform> & poses,
		RegistrationInfo * info)
{
	UASSERT(uContains(poses, fromId) && uContains(_signatures, fromId));
	UASSERT(uContains(poses, toId) && uContains(_signatures, toId));

	UDEBUG("%d -> %d, Guess=%s", fromId, toId, (poses.at(fromId).inverse() * poses.at(toId)).prettyPrint().c_str());
	if(ULogger::level() == ULogger::kDebug)
	{
		std::string ids;
		for(std::map<int, Transform>::const_iterator iter = poses.begin(); iter!=poses.end(); ++iter)
		{
			if(iter->first != fromId)
			{
				ids += uNumber2Str(iter->first) + " ";
			}
		}
		UDEBUG("%d vs %s", fromId, ids.c_str());
	}

	// make sure that all laser scans are loaded
	std::list<Signature*> depthToLoad;
	for(std::map<int, Transform>::const_iterator iter = poses.begin(); iter!=poses.end(); ++iter)
	{
		Signature * s = _getSignature(iter->first);
		UASSERT_MSG(s != 0, uFormat("id=%d", iter->first).c_str());
		//if image is already here, scan should be or it is null
		if(s->sensorData().imageCompressed().empty() &&
		   s->sensorData().laserScanCompressed().isEmpty())
		{
			depthToLoad.push_back(s);
		}
	}
	if(depthToLoad.size() && _dbDriver)
	{
		_dbDriver->loadNodeData(depthToLoad, false, true, false, false);
	}

	Signature * fromS = _getSignature(fromId);
	LaserScan fromScan;
	fromS->sensorData().uncompressData(0, 0, &fromScan);

	Signature * toS = _getSignature(toId);
	LaserScan toScan;
	toS->sensorData().uncompressData(0, 0, &toScan);

	Transform t;
	if(!fromScan.isEmpty() && !toScan.isEmpty())
	{
		Transform guess = poses.at(fromId).inverse() * poses.at(toId);
		float guessNorm = guess.getNorm();
		if(fromScan.rangeMax() > 0.0f && toScan.rangeMax() > 0.0f &&
			guessNorm > fromScan.rangeMax() + toScan.rangeMax())
		{
			// stop right known,it is impossible that scans overlay.
			UINFO("Too far scans between %d and %d to compute transformation: guessNorm=%f, scan range from=%f to=%f", fromId, toId, guessNorm, fromScan.rangeMax(), toScan.rangeMax());
			return t;
		}

		// Create a fake signature with all scans merged in oldId referential
		SensorData assembledData;
		Transform toPoseInv = poses.at(toId).inverse();
		std::string msg;
		int maxPoints = fromScan.size();
		pcl::PointCloud<pcl::PointXYZ>::Ptr assembledToClouds(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointNormal>::Ptr assembledToNormalClouds(new pcl::PointCloud<pcl::PointNormal>);
		pcl::PointCloud<pcl::PointXYZI>::Ptr assembledToIClouds(new pcl::PointCloud<pcl::PointXYZI>);
		pcl::PointCloud<pcl::PointXYZINormal>::Ptr assembledToNormalIClouds(new pcl::PointCloud<pcl::PointXYZINormal>);
		for(std::map<int, Transform>::const_iterator iter = poses.begin(); iter!=poses.end(); ++iter)
		{
			if(iter->first != fromId)
			{
				Signature * s = this->_getSignature(iter->first);
				if(!s->sensorData().laserScanCompressed().isEmpty())
				{
					LaserScan scan;
					s->sensorData().uncompressData(0, 0, &scan);
					if(!scan.isEmpty() && scan.format() == fromScan.format())
					{
						if(scan.hasIntensity())
						{
							if(scan.hasNormals())
							{
								*assembledToNormalIClouds += *util3d::laserScanToPointCloudINormal(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
							else
							{
								*assembledToIClouds += *util3d::laserScanToPointCloudI(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
						}
						else
						{
							if(scan.hasNormals())
							{
								*assembledToNormalClouds += *util3d::laserScanToPointCloudNormal(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
							else
							{
								*assembledToClouds += *util3d::laserScanToPointCloud(scan,
										toPoseInv * iter->second * scan.localTransform());
							}
						}

						if(scan.size() > maxPoints)
						{
							maxPoints = scan.size();
						}
					}
					else if(!scan.isEmpty())
					{
						UWARN("Incompatible scan format %d vs %d", (int)fromScan.format(), (int)scan.format());
					}
				}
				else
				{
					UWARN("Depth2D not found for signature %d", iter->first);
				}
			}
		}

		cv::Mat assembledScan;
		if(assembledToNormalClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToNormalClouds):util3d::laserScanFromPointCloud(*assembledToNormalClouds);
		}
		else if(assembledToClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToClouds):util3d::laserScanFromPointCloud(*assembledToClouds);
		}
		else if(assembledToNormalIClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToNormalIClouds):util3d::laserScanFromPointCloud(*assembledToNormalIClouds);
		}
		else if(assembledToIClouds->size())
		{
			assembledScan = fromScan.is2d()?util3d::laserScan2dFromPointCloud(*assembledToIClouds):util3d::laserScanFromPointCloud(*assembledToIClouds);
		}

		// scans are in base frame but for 2d scans, set the height so that correspondences matching works
		assembledData.setLaserScan(
				LaserScan(assembledScan,
					fromScan.maxPoints()?fromScan.maxPoints():maxPoints,
					fromScan.rangeMax(),
					fromScan.format(),
					fromScan.is2d()?Transform(0,0,fromScan.localTransform().z(),0,0,0):Transform::getIdentity()));

		t = _registrationIcpMulti->computeTransformation(fromS->sensorData(), assembledData, guess, info);
	}

	return t;
}

bool Memory::addLink(const Link & link, bool addInDatabase)
{
	UASSERT(link.type() > Link::kNeighbor && link.type() != Link::kUndef);

	ULOGGER_INFO("to=%d, from=%d transform: %s var=%f", link.to(), link.from(), link.transform().prettyPrint().c_str(), link.transVariance());
	Signature * toS = _getSignature(link.to());
	Signature * fromS = _getSignature(link.from());
	if(toS && fromS)
	{
		if(toS->hasLink(link.from()))
		{
			// do nothing, already merged
			UINFO("already linked! to=%d, from=%d", link.to(), link.from());
			return true;
		}

		UDEBUG("Add link between %d and %d", toS->id(), fromS->id());

		toS->addLink(link.inverse());
		fromS->addLink(link);

		if(_incrementalMemory)
		{
			if(link.type()!=Link::kVirtualClosure)
			{
				_linksChanged = true;

				// update weight
				// ignore scan matching loop closures
				if(link.type() != Link::kLocalSpaceClosure ||
				   link.userDataCompressed().empty())
				{
					_lastGlobalLoopClosureId = fromS->id()>toS->id()?fromS->id():toS->id();

					// update weights only if the memory is incremental
					// When reducing the graph, transfer weight to the oldest signature
					UASSERT(fromS->getWeight() >= 0 && toS->getWeight() >=0);
					if((_reduceGraph && fromS->id() < toS->id()) ||
					   (!_reduceGraph && fromS->id() > toS->id()))
					{
						fromS->setWeight(fromS->getWeight() + toS->getWeight());
						toS->setWeight(0);
					}
					else
					{
						toS->setWeight(toS->getWeight() + fromS->getWeight());
						fromS->setWeight(0);
					}
				}
			}
		}
	}
	else if(!addInDatabase)
	{
		if(!fromS)
		{
			UERROR("from=%d, to=%d, Signature %d not found in working/st memories", link.from(), link.to(), link.from());
		}
		if(!toS)
		{
			UERROR("from=%d, to=%d, Signature %d not found in working/st memories", link.from(), link.to(), link.to());
		}
		return false;
	}
	else if(fromS)
	{
		UDEBUG("Add link between %d and %d (db)", link.from(), link.to());
		fromS->addLink(link);
		_dbDriver->addLink(link.inverse());
	}
	else if(toS)
	{
		UDEBUG("Add link between %d (db) and %d", link.from(), link.to());
		_dbDriver->addLink(link);
		toS->addLink(link.inverse());
	}
	else
	{
		UDEBUG("Add link between %d (db) and %d (db)", link.from(), link.to());
		_dbDriver->addLink(link);
		_dbDriver->addLink(link.inverse());
	}
	return true;
}

void Memory::updateLink(const Link & link, bool updateInDatabase)
{
	Signature * fromS = this->_getSignature(link.from());
	Signature * toS = this->_getSignature(link.to());

	if(fromS && toS)
	{
		if(fromS->hasLink(link.to()) && toS->hasLink(link.from()))
		{
			Link::Type oldType = fromS->getLinks().at(link.to()).type();

			fromS->removeLink(link.to());
			toS->removeLink(link.from());

			fromS->addLink(link);
			toS->addLink(link.inverse());

			if(oldType!=Link::kVirtualClosure || link.type()!=Link::kVirtualClosure)
			{
				_linksChanged = true;
			}
		}
		else
		{
			UERROR("fromId=%d and toId=%d are not linked!", link.from(), link.to());
		}
	}
	else if(!updateInDatabase)
	{
		if(!fromS)
		{
			UERROR("from=%d, to=%d, Signature %d not found in working/st memories", link.from(), link.to(), link.from());
		}
		if(!toS)
		{
			UERROR("from=%d, to=%d, Signature %d not found in working/st memories", link.from(), link.to(), link.to());
		}
	}
	else if(fromS)
	{
		UDEBUG("Update link between %d and %d (db)", link.from(), link.to());
		fromS->removeLink(link.to());
		fromS->addLink(link);
		_dbDriver->updateLink(link.inverse());
	}
	else if(toS)
	{
		UDEBUG("Update link between %d (db) and %d", link.from(), link.to());
		toS->removeLink(link.from());
		toS->addLink(link.inverse());
		_dbDriver->updateLink(link);
	}
	else
	{
		UDEBUG("Update link between %d (db) and %d (db)", link.from(), link.to());
		_dbDriver->updateLink(link);
		_dbDriver->updateLink(link.inverse());
	}
}

void Memory::removeAllVirtualLinks()
{
	UDEBUG("");
	for(std::map<int, Signature*>::iterator iter=_signatures.begin(); iter!=_signatures.end(); ++iter)
	{
		iter->second->removeVirtualLinks();
	}
}

void Memory::removeVirtualLinks(int signatureId)
{
	UDEBUG("");
	Signature * s = this->_getSignature(signatureId);
	if(s)
	{
		const std::map<int, Link> & links = s->getLinks();
		for(std::map<int, Link>::const_iterator iter=links.begin(); iter!=links.end(); ++iter)
		{
			if(iter->second.type() == Link::kVirtualClosure)
			{
				Signature * sTo = this->_getSignature(iter->first);
				if(sTo)
				{
					sTo->removeLink(s->id());
				}
				else
				{
					UERROR("Link %d of %d not in WM/STM?!?", iter->first, s->id());
				}
			}
		}
		s->removeVirtualLinks();
	}
	else
	{
		UERROR("Signature %d not in WM/STM?!?", signatureId);
	}
}

void Memory::dumpMemory(std::string directory) const
{
	UINFO("Dumping memory to directory \"%s\"", directory.c_str());
	this->dumpDictionary((directory+"/DumpMemoryWordRef.txt").c_str(), (directory+"/DumpMemoryWordDesc.txt").c_str());
	this->dumpSignatures((directory + "/DumpMemorySign.txt").c_str(), false);
	this->dumpSignatures((directory + "/DumpMemorySign3.txt").c_str(), true);
	this->dumpMemoryTree((directory + "/DumpMemoryTree.txt").c_str());
}

void Memory::dumpDictionary(const char * fileNameRef, const char * fileNameDesc) const
{
	if(_vwd)
	{
		_vwd->exportDictionary(fileNameRef, fileNameDesc);
	}
}

void Memory::dumpSignatures(const char * fileNameSign, bool words3D) const
{
	UDEBUG("");
	FILE* foutSign = 0;
#ifdef _MSC_VER
	fopen_s(&foutSign, fileNameSign, "w");
#else
	foutSign = fopen(fileNameSign, "w");
#endif

	if(foutSign)
	{
		fprintf(foutSign, "SignatureID WordsID...\n");
		const std::map<int, Signature *> & signatures = this->getSignatures();
		for(std::map<int, Signature *>::const_iterator iter=signatures.begin(); iter!=signatures.end(); ++iter)
		{
			fprintf(foutSign, "%d ", iter->first);
			const Signature * ss = dynamic_cast<const Signature *>(iter->second);
			if(ss)
			{
				if(words3D)
				{
					const std::multimap<int, cv::Point3f> & ref = ss->getWords3();
					for(std::multimap<int, cv::Point3f>::const_iterator jter=ref.begin(); jter!=ref.end(); ++jter)
					{
						//show only valid point according to current parameters
						if(pcl::isFinite(jter->second) &&
						   (jter->second.x != 0 || jter->second.y != 0 || jter->second.z != 0))
						{
							fprintf(foutSign, "%d ", (*jter).first);
						}
					}
				}
				else
				{
					const std::multimap<int, cv::KeyPoint> & ref = ss->getWords();
					for(std::multimap<int, cv::KeyPoint>::const_iterator jter=ref.begin(); jter!=ref.end(); ++jter)
					{
						fprintf(foutSign, "%d ", (*jter).first);
					}
				}
			}
			fprintf(foutSign, "\n");
		}
		fclose(foutSign);
	}
}

void Memory::dumpMemoryTree(const char * fileNameTree) const
{
	UDEBUG("");
	FILE* foutTree = 0;
	#ifdef _MSC_VER
		fopen_s(&foutTree, fileNameTree, "w");
	#else
		foutTree = fopen(fileNameTree, "w");
	#endif

	if(foutTree)
	{
		fprintf(foutTree, "SignatureID Weight NbLoopClosureIds LoopClosureIds... NbChildLoopClosureIds ChildLoopClosureIds...\n");

		for(std::map<int, Signature *>::const_iterator i=_signatures.begin(); i!=_signatures.end(); ++i)
		{
			fprintf(foutTree, "%d %d", i->first, i->second->getWeight());

			std::map<int, Link> loopIds, childIds;

			for(std::map<int, Link>::const_iterator iter = i->second->getLinks().begin();
					iter!=i->second->getLinks().end();
					++iter)
			{
				if(iter->second.type() != Link::kNeighbor &&
			       iter->second.type() != Link::kNeighborMerged)
				{
					if(iter->first < i->first)
					{
						childIds.insert(*iter);
					}
					else if(iter->second.from() != iter->second.to())
					{
						loopIds.insert(*iter);
					}
				}
			}

			fprintf(foutTree, " %d", (int)loopIds.size());
			for(std::map<int, Link>::const_iterator j=loopIds.begin(); j!=loopIds.end(); ++j)
			{
				fprintf(foutTree, " %d", j->first);
			}

			fprintf(foutTree, " %d", (int)childIds.size());
			for(std::map<int, Link>::const_iterator j=childIds.begin(); j!=childIds.end(); ++j)
			{
				fprintf(foutTree, " %d", j->first);
			}

			fprintf(foutTree, "\n");
		}

		fclose(foutTree);
	}

}

void Memory::rehearsal(Signature * signature, Statistics * stats)
{
	UTimer timer;
	if(signature->isBadSignature())
	{
		return;
	}

	//============================================================
	// Compare with the last (not intermediate node)
	//============================================================
	Signature * sB = 0;
	for(std::set<int>::reverse_iterator iter=_stMem.rbegin(); iter!=_stMem.rend(); ++iter)
	{
		Signature * s = this->_getSignature(*iter);
		UASSERT(s!=0);
		if(s->getWeight() >= 0 && s->id() != signature->id())
		{
			sB = s;
			break;
		}
	}
	if(sB)
	{
		int id = sB->id();
		UDEBUG("Comparing with signature (%d)...", id);

		float sim = signature->compareTo(*sB);

		int merged = 0;
		if(sim >= _similarityThreshold)
		{
			if(_incrementalMemory)
			{
				if(this->rehearsalMerge(id, signature->id()))
				{
					merged = id;
				}
			}
			else
			{
				signature->setWeight(signature->getWeight() + 1 + sB->getWeight());
			}
		}

		if(stats) stats->addStatistic(Statistics::kMemoryRehearsal_merged(), merged);
		if(stats) stats->addStatistic(Statistics::kMemoryRehearsal_sim(), sim);
		if(stats) stats->addStatistic(Statistics::kMemoryRehearsal_id(), sim >= _similarityThreshold?id:0);
		UDEBUG("merged=%d, sim=%f t=%fs", merged, sim, timer.ticks());
	}
	else
	{
		if(stats) stats->addStatistic(Statistics::kMemoryRehearsal_merged(), 0);
		if(stats) stats->addStatistic(Statistics::kMemoryRehearsal_sim(), 0);
	}
}

bool Memory::rehearsalMerge(int oldId, int newId)
{
	ULOGGER_INFO("old=%d, new=%d", oldId, newId);
	Signature * oldS = _getSignature(oldId);
	Signature * newS = _getSignature(newId);
	if(oldS && newS && _incrementalMemory)
	{
		UASSERT_MSG(oldS->getWeight() >= 0 && newS->getWeight() >= 0, uFormat("%d %d", oldS->getWeight(), newS->getWeight()).c_str());
		std::map<int, Link>::const_iterator iter = oldS->getLinks().find(newS->id());
		if(iter != oldS->getLinks().end() &&
		   iter->second.type() != Link::kNeighbor &&
		   iter->second.type() != Link::kNeighborMerged &&
		   iter->second.from() != iter->second.to())
		{
			// do nothing, already merged
			UWARN("already merged, old=%d, new=%d", oldId, newId);
			return false;
		}
		UASSERT(!newS->isSaved());

		UINFO("Rehearsal merging %d (w=%d) and %d (w=%d)",
				oldS->id(), oldS->getWeight(),
				newS->id(), newS->getWeight());

		bool fullMerge;
		bool intermediateMerge = false;
		if(!newS->getLinks().empty() && !newS->getLinks().begin()->second.transform().isNull())
		{
			// we are in metric SLAM mode:
			// 1) Normal merge if not moving AND has direct link
			// 2) Transform to intermediate node (weight = -1) if not moving AND hasn't direct link.
			float x,y,z, roll,pitch,yaw;
			newS->getLinks().begin()->second.transform().getTranslationAndEulerAngles(x,y,z, roll,pitch,yaw);
			bool isMoving = fabs(x) > _rehearsalMaxDistance ||
							fabs(y) > _rehearsalMaxDistance ||
							fabs(z) > _rehearsalMaxDistance ||
							fabs(roll) > _rehearsalMaxAngle ||
							fabs(pitch) > _rehearsalMaxAngle ||
							fabs(yaw) > _rehearsalMaxAngle;
			if(isMoving && _rehearsalWeightIgnoredWhileMoving)
			{
				UINFO("Rehearsal ignored because the robot has moved more than %f m or %f rad (\"Mem/RehearsalWeightIgnoredWhileMoving\"=true)",
						_rehearsalMaxDistance, _rehearsalMaxAngle);
				return false;
			}
			fullMerge = !isMoving && newS->hasLink(oldS->id());
			intermediateMerge = !isMoving && !newS->hasLink(oldS->id());
		}
		else
		{
			fullMerge = newS->hasLink(oldS->id()) && newS->getLinks().begin()->second.transform().isNull();
		}

		if(fullMerge)
		{
			//remove mutual links
			Link newToOldLink = newS->getLinks().at(oldS->id());
			oldS->removeLink(newId);
			newS->removeLink(oldId);

			if(_idUpdatedToNewOneRehearsal)
			{
				// redirect neighbor links
				const std::map<int, Link> & links = oldS->getLinks();
				for(std::map<int, Link>::const_iterator iter = links.begin(); iter!=links.end(); ++iter)
				{
					if(iter->second.from() != iter->second.to())
					{
						Link link = iter->second;
						Link mergedLink = newToOldLink.merge(link, link.type());
						UASSERT(mergedLink.from() == newS->id() && mergedLink.to() == link.to());

						Signature * s = this->_getSignature(link.to());
						if(s)
						{
							// modify neighbor "from"
							s->removeLink(oldS->id());
							s->addLink(mergedLink.inverse());

							newS->addLink(mergedLink);
						}
						else
						{
							UERROR("Didn't find neighbor %d of %d in RAM...", link.to(), oldS->id());
						}
					}
				}
				newS->setLabel(oldS->getLabel());
				oldS->setLabel("");
				oldS->removeLinks(); // remove all links
				oldS->addLink(Link(oldS->id(), newS->id(), Link::kGlobalClosure, Transform(), cv::Mat::eye(6,6,CV_64FC1))); // to keep track of the merged location

				// Set old image to new signature
				this->copyData(oldS, newS);

				// update weight
				newS->setWeight(newS->getWeight() + 1 + oldS->getWeight());

				if(_lastGlobalLoopClosureId == oldS->id())
				{
					_lastGlobalLoopClosureId = newS->id();
				}
			}
			else
			{
				newS->addLink(Link(newS->id(), oldS->id(), Link::kGlobalClosure, Transform() , cv::Mat::eye(6,6,CV_64FC1))); // to keep track of the merged location

				// update weight
				oldS->setWeight(newS->getWeight() + 1 + oldS->getWeight());

				if(_lastSignature == newS)
				{
					_lastSignature = oldS;
				}
			}

			// remove location
			moveToTrash(_idUpdatedToNewOneRehearsal?oldS:newS, _notLinkedNodesKeptInDb);

			return true;
		}
		else
		{
			// update only weights
			if(_idUpdatedToNewOneRehearsal)
			{
				// just update weight
				int w = oldS->getWeight()>=0?oldS->getWeight():0;
				newS->setWeight(w + newS->getWeight() + 1);
				oldS->setWeight(intermediateMerge?-1:0); // convert to intermediate node

				if(_lastGlobalLoopClosureId == oldS->id())
				{
					_lastGlobalLoopClosureId = newS->id();
				}
			}
			else // !_idUpdatedToNewOneRehearsal
			{
				int w = newS->getWeight()>=0?newS->getWeight():0;
				oldS->setWeight(w + oldS->getWeight() + 1);
				newS->setWeight(intermediateMerge?-1:0); // convert to intermediate node
			}
		}
	}
	else
	{
		if(!newS)
		{
			UERROR("newId=%d, oldId=%d, Signature %d not found in working/st memories", newId, oldId, newId);
		}
		if(!oldS)
		{
			UERROR("newId=%d, oldId=%d, Signature %d not found in working/st memories", newId, oldId, oldId);
		}
	}
	return false;
}

int Memory::getMapId(int signatureId, bool lookInDatabase) const
{
	Transform pose, groundTruth;
	int mapId = -1, weight;
	std::string label;
	double stamp;
	std::vector<float> velocity;
	GPS gps;
	EnvSensors sensors;
	getNodeInfo(signatureId, pose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors, lookInDatabase);
	return mapId;
}

Transform Memory::getOdomPose(int signatureId, bool lookInDatabase) const
{
	Transform pose, groundTruth;
	int mapId, weight;
	std::string label;
	double stamp;
	std::vector<float> velocity;
	GPS gps;
	EnvSensors sensors;
	getNodeInfo(signatureId, pose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors, lookInDatabase);
	return pose;
}

Transform Memory::getGroundTruthPose(int signatureId, bool lookInDatabase) const
{
	Transform pose, groundTruth;
	int mapId, weight;
	std::string label;
	double stamp;
	std::vector<float> velocity;
	GPS gps;
	EnvSensors sensors;
	getNodeInfo(signatureId, pose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors, lookInDatabase);
	return groundTruth;
}

void Memory::getGPS(int id, GPS & gps, Transform & offsetENU, bool lookInDatabase, int maxGraphDepth) const
{
	gps = GPS();
	offsetENU=Transform::getIdentity();

	Transform odomPose, groundTruth;
	int mapId, weight;
	std::string label;
	double stamp;
	std::vector<float> velocity;
	EnvSensors sensors;
	getNodeInfo(id, odomPose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors, lookInDatabase);

	if(gps.stamp() == 0.0)
	{
		// Search for the nearest node with GPS, and compute its relative transform in ENU coordinates

		std::map<int, int> nearestIds;
		nearestIds = getNeighborsId(id, maxGraphDepth, lookInDatabase?-1:0, true, false, true);
		std::multimap<int, int> nearestIdsSorted;
		for(std::map<int, int>::iterator iter=nearestIds.begin(); iter!=nearestIds.end(); ++iter)
		{
			nearestIdsSorted.insert(std::make_pair(iter->second, iter->first));
		}

		for(std::map<int, int>::iterator iter=nearestIdsSorted.begin(); iter!=nearestIdsSorted.end(); ++iter)
		{
			const Signature * s = getSignature(iter->second);
			UASSERT(s!=0);
			if(s->sensorData().gps().stamp() > 0.0)
			{
				std::list<std::pair<int, Transform> > path = graph::computePath(s->id(), id, this, lookInDatabase);
				if(path.size() >= 2)
				{
					gps = s->sensorData().gps();
					Transform localToENU(0,0,(float)((-(gps.bearing()-90))*M_PI/180.0) - s->getPose().theta());
					offsetENU = localToENU*(s->getPose().rotation()*path.rbegin()->second);
					break;
				}
				else
				{
					UWARN("Failed to find path %d -> %d", s->id(), id);
				}
			}
		}
	}
}

bool Memory::getNodeInfo(int signatureId,
		Transform & odomPose,
		int & mapId,
		int & weight,
		std::string & label,
		double & stamp,
		Transform & groundTruth,
		std::vector<float> & velocity,
		GPS & gps,
		EnvSensors & sensors,
		bool lookInDatabase) const
{
	const Signature * s = this->getSignature(signatureId);
	if(s)
	{
		odomPose = s->getPose();
		mapId = s->mapId();
		weight = s->getWeight();
		label = s->getLabel();
		stamp = s->getStamp();
		groundTruth = s->getGroundTruthPose();
		velocity = s->getVelocity();
		gps = s->sensorData().gps();
		sensors = s->sensorData().envSensors();
		return true;
	}
	else if(lookInDatabase && _dbDriver)
	{
		return _dbDriver->getNodeInfo(signatureId, odomPose, mapId, weight, label, stamp, groundTruth, velocity, gps, sensors);
	}
	return false;
}

cv::Mat Memory::getImageCompressed(int signatureId) const
{
	cv::Mat image;
	const Signature * s = this->getSignature(signatureId);
	if(s)
	{
		image = s->sensorData().imageCompressed();
	}
	if(image.empty() && this->isBinDataKept() && _dbDriver)
	{
		SensorData data;
		_dbDriver->getNodeData(signatureId, data);
		image = data.imageCompressed();
	}
	return image;
}

SensorData Memory::getNodeData(int nodeId, bool uncompressedData) const
{
	//UDEBUG("nodeId=%d", nodeId);
	SensorData r;
	Signature * s = this->_getSignature(nodeId);
	if(s && !s->sensorData().imageCompressed().empty())
	{
		r = s->sensorData();
	}
	else if(_dbDriver)
	{
		// load from database
		_dbDriver->getNodeData(nodeId, r);
	}

	if(uncompressedData)
	{
		r.uncompressData();
	}

	return r;
}

void Memory::getNodeWords(int nodeId,
		std::multimap<int, cv::KeyPoint> & words,
		std::multimap<int, cv::Point3f> & words3,
		std::multimap<int, cv::Mat> & wordsDescriptors)
{
	//UDEBUG("nodeId=%d", nodeId);
	Signature * s = this->_getSignature(nodeId);
	if(s)
	{
		words = s->getWords();
		words3 = s->getWords3();
		wordsDescriptors = s->getWordsDescriptors();
	}
	else if(_dbDriver)
	{
		// load from database
		std::list<Signature*> signatures;
		std::list<int> ids;
		ids.push_back(nodeId);
		std::set<int> loadedFromTrash;
		_dbDriver->loadSignatures(ids, signatures, &loadedFromTrash);
		if(signatures.size())
		{
			words = signatures.front()->getWords();
			words3 = signatures.front()->getWords3();
			wordsDescriptors = signatures.front()->getWordsDescriptors();
			if(loadedFromTrash.size())
			{
				//put back
				_dbDriver->asyncSave(signatures.front());
			}
			else
			{
				delete signatures.front();
			}
		}
	}
}

void Memory::getNodeCalibration(int nodeId,
		std::vector<CameraModel> & models,
		StereoCameraModel & stereoModel)
{
	//UDEBUG("nodeId=%d", nodeId);
	Signature * s = this->_getSignature(nodeId);
	if(s)
	{
		models = s->sensorData().cameraModels();
		stereoModel = s->sensorData().stereoCameraModel();
	}
	else if(_dbDriver)
	{
		// load from database
		_dbDriver->getCalibration(nodeId, models, stereoModel);
	}
}

SensorData Memory::getSignatureDataConst(int locationId,
		bool images, bool scan, bool userData, bool occupancyGrid) const
{
	//UDEBUG("");
	SensorData r;
	const Signature * s = this->getSignature(locationId);
	if(s && (!s->sensorData().imageCompressed().empty() ||
			!s->sensorData().laserScanCompressed().isEmpty() ||
			!s->sensorData().userDataCompressed().empty() ||
			s->sensorData().gridCellSize() != 0.0f))
	{
		r = s->sensorData();
	}
	else if(_dbDriver)
	{
		// load from database
		_dbDriver->getNodeData(locationId, r, images, scan, userData, occupancyGrid);
	}

	return r;
}

void Memory::generateGraph(const std::string & fileName, const std::set<int> & ids)
{
	if(!_dbDriver)
	{
		UERROR("A database must must loaded first...");
		return;
	}

	_dbDriver->generateGraph(fileName, ids, _signatures);
}

int Memory::getNi(int signatureId) const
{
	int ni = 0;
	const Signature * s = this->getSignature(signatureId);
	if(s)
	{
		ni = (int)((Signature *)s)->getWords().size();
	}
	else
	{
		_dbDriver->getInvertedIndexNi(signatureId, ni);
	}
	return ni;
}


void Memory::copyData(const Signature * from, Signature * to)
{
	UTimer timer;
	timer.start();
	if(from && to)
	{
		// words 2d
		this->disableWordsRef(to->id());
		to->setWords(from->getWords());
		std::list<int> id;
		id.push_back(to->id());
		this->enableWordsRef(id);

		if(from->isSaved() && _dbDriver)
		{
			_dbDriver->getNodeData(from->id(), to->sensorData());
			UDEBUG("Loaded image data from database");
		}
		else
		{
			to->sensorData() = (SensorData)from->sensorData();
		}
		to->sensorData().setId(to->id());

		to->setPose(from->getPose());
		to->setWords3(from->getWords3());
		to->setWordsDescriptors(from->getWordsDescriptors());
	}
	else
	{
		ULOGGER_ERROR("Can't merge the signatures because there are not same type.");
	}
	UDEBUG("Merging time = %fs", timer.ticks());
}

class PreUpdateThread : public UThreadNode
{
public:
	PreUpdateThread(VWDictionary * vwp) : _vwp(vwp) {}
	virtual ~PreUpdateThread() {}
private:
	void mainLoop() {
		if(_vwp)
		{
			_vwp->update();
		}
		this->kill();
	}
	VWDictionary * _vwp;
};

Signature * Memory::createSignature(const SensorData & inputData, const Transform & pose, Statistics * stats)
{
	UDEBUG("");
	SensorData data = inputData;
	UASSERT(data.imageRaw().empty() ||
			data.imageRaw().type() == CV_8UC1 ||
			data.imageRaw().type() == CV_8UC3);
	UASSERT_MSG(data.depthOrRightRaw().empty() ||
			(  ( data.depthOrRightRaw().type() == CV_16UC1 ||
				 data.depthOrRightRaw().type() == CV_32FC1 ||
				 data.depthOrRightRaw().type() == CV_8UC1)
			   &&
				( (data.imageRaw().empty() && data.depthOrRightRaw().type() != CV_8UC1) ||
				  (data.imageRaw().rows % data.depthOrRightRaw().rows == 0 && data.imageRaw().cols % data.depthOrRightRaw().cols == 0))),
				uFormat("image=(%d/%d) depth=(%d/%d, type=%d [accepted=%d,%d,%d])",
						data.imageRaw().cols,
						data.imageRaw().rows,
						data.depthOrRightRaw().cols,
						data.depthOrRightRaw().rows,
						data.depthOrRightRaw().type(),
						CV_16UC1, CV_32FC1, CV_8UC1).c_str());

	if(!data.depthOrRightRaw().empty() &&
		data.cameraModels().size() == 0 &&
		!data.stereoCameraModel().isValidForProjection() &&
		!pose.isNull())
	{
		UERROR("Camera calibration not valid, calibrate your camera!");
		return 0;
	}
	UASSERT(_feature2D != 0);

	PreUpdateThread preUpdateThread(_vwd);

	UTimer timer;
	timer.start();
	float t;
	std::vector<cv::KeyPoint> keypoints;
	cv::Mat descriptors;
	bool isIntermediateNode = data.id() < 0 || (data.imageRaw().empty() && data.keypoints().empty());
	int id = data.id();
	if(_generateIds)
	{
		id = this->getNextId();
	}
	else
	{
		if(id <= 0)
		{
			UERROR("Received image ID is null. "
				  "Please set parameter Mem/GenerateIds to \"true\" or "
				  "make sure the input source provides image ids (seq).");
			return 0;
		}
		else if(id > _idCount)
		{
			_idCount = id;
		}
		else
		{
			UERROR("Id of acquired image (%d) is smaller than the last in memory (%d). "
				  "Please set parameter Mem/GenerateIds to \"true\" or "
				  "make sure the input source provides image ids (seq) over the last in "
				  "memory, which is %d.",
					id,
					_idCount,
					_idCount);
			return 0;
		}
	}

	bool imagesRectified = _imagesAlreadyRectified;
	// Stereo must be always rectified because of the stereo correspondence approach
	if(!imagesRectified && !data.imageRaw().empty() && !(_rectifyOnlyFeatures && data.rightRaw().empty()))
	{
		// we assume that once rtabmap is receiving data, the calibration won't change over time
		if(data.cameraModels().size())
		{
			// Note that only RGB image is rectified, the depth image is assumed to be already registered to rectified RGB camera.
			UASSERT(int((data.imageRaw().cols/data.cameraModels().size())*data.cameraModels().size()) == data.imageRaw().cols);
			int subImageWidth = data.imageRaw().cols/data.cameraModels().size();
			cv::Mat rectifiedImages(data.imageRaw().size(), data.imageRaw().type());
			bool initRectMaps = _rectCameraModels.empty();
			if(initRectMaps)
			{
				_rectCameraModels.resize(data.cameraModels().size());
			}
			for(unsigned int i=0; i<data.cameraModels().size(); ++i)
			{
				if(data.cameraModels()[i].isValidForRectification())
				{
					if(initRectMaps)
					{
						_rectCameraModels[i] = data.cameraModels()[i];
						if(!_rectCameraModels[i].isRectificationMapInitialized())
						{
							UWARN("Initializing rectification maps for camera %d (only done for the first image received)...", i);
							_rectCameraModels[i].initRectificationMap();
							UWARN("Initializing rectification maps for camera %d (only done for the first image received)... done!", i);
						}
					}
					UASSERT(_rectCameraModels[i].imageWidth() == data.cameraModels()[i].imageWidth() &&
							_rectCameraModels[i].imageHeight() == data.cameraModels()[i].imageHeight());
					cv::Mat rectifiedImage = _rectCameraModels[i].rectifyImage(cv::Mat(data.imageRaw(), cv::Rect(subImageWidth*i, 0, subImageWidth, data.imageRaw().rows)));
					rectifiedImage.copyTo(cv::Mat(rectifiedImages, cv::Rect(subImageWidth*i, 0, subImageWidth, data.imageRaw().rows)));
					imagesRectified = true;
				}
				else
				{
					UERROR("Calibration for camera %d cannot be used to rectify the image. Make sure to do a "
						"full calibration. If images are already rectified, set %s parameter back to true.",
						(int)i,
						Parameters::kRtabmapImagesAlreadyRectified().c_str());
					return 0;
				}
			}
			data.setRGBDImage(rectifiedImages, data.depthOrRightRaw(), data.cameraModels());
		}
		else if(data.stereoCameraModel().isValidForRectification())
		{
			if(!_rectStereoCameraModel.isValidForRectification())
			{
				_rectStereoCameraModel = data.stereoCameraModel();
				if(!_rectStereoCameraModel.isRectificationMapInitialized())
				{
					UWARN("Initializing rectification maps (only done for the first image received)...");
					_rectStereoCameraModel.initRectificationMap();
					UWARN("Initializing rectification maps (only done for the first image received)...done!");
				}
			}
			UASSERT(_rectStereoCameraModel.left().imageWidth() == data.stereoCameraModel().left().imageWidth());
			UASSERT(_rectStereoCameraModel.left().imageHeight() == data.stereoCameraModel().left().imageHeight());
			data.setStereoImage(
					_rectStereoCameraModel.left().rectifyImage(data.imageRaw()),
					_rectStereoCameraModel.right().rectifyImage(data.rightRaw()),
					data.stereoCameraModel());
			imagesRectified = true;
		}
		else
		{
			UERROR("Stereo calibration cannot be used to rectify images. Make sure to do a "
					"full stereo calibration. If images are already rectified, set %s parameter back to true.",
					Parameters::kRtabmapImagesAlreadyRectified().c_str());
			return 0;
		}
		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemRectification(), t*1000.0f);
		UDEBUG("time rectification = %fs", t);
	}

	int treeSize= int(_workingMem.size() + _stMem.size());
	int meanWordsPerLocation = _feature2D->getMaxFeatures()>0?_feature2D->getMaxFeatures():0;
	if(treeSize > 1)
	{
		meanWordsPerLocation = _vwd->getTotalActiveReferences() / (treeSize-1); // ignore virtual signature
	}

	if(_parallelized && !isIntermediateNode)
	{
		UDEBUG("Start dictionary update thread");
		preUpdateThread.start();
	}

	int preDecimation = 1;
	std::vector<cv::Point3f> keypoints3D;
	SensorData decimatedData;
	if(!_useOdometryFeatures ||
		data.keypoints().empty() ||
		(int)data.keypoints().size() != data.descriptors().rows ||
		(_feature2D->getType() == Feature2D::kFeatureOrbOctree && data.descriptors().empty()))
	{
		if(_feature2D->getMaxFeatures() >= 0 && !data.imageRaw().empty() && !isIntermediateNode)
		{
			decimatedData = data;
			if(_imagePreDecimation > 1)
			{
				preDecimation = _imagePreDecimation;
				std::vector<CameraModel> cameraModels = decimatedData.cameraModels();
				for(unsigned int i=0; i<cameraModels.size(); ++i)
				{
					cameraModels[i] = cameraModels[i].scaled(1.0/double(_imagePreDecimation));
				}
				if(!cameraModels.empty())
				{
					if(decimatedData.depthRaw().rows == decimatedData.imageRaw().rows &&
					   decimatedData.depthRaw().cols == decimatedData.imageRaw().cols)
					{
						decimatedData.setRGBDImage(
								util2d::decimate(decimatedData.imageRaw(), _imagePreDecimation),
								util2d::decimate(decimatedData.depthOrRightRaw(), _imagePreDecimation),
								cameraModels);
					}
					else
					{
						decimatedData.setRGBDImage(
								util2d::decimate(decimatedData.imageRaw(), _imagePreDecimation),
								decimatedData.depthOrRightRaw(),
								cameraModels);
					}
				}
				else
				{
					StereoCameraModel stereoModel = decimatedData.stereoCameraModel();
					if(stereoModel.isValidForProjection())
					{
						stereoModel.scale(1.0/double(_imagePreDecimation));
					}
					decimatedData.setStereoImage(
							util2d::decimate(decimatedData.imageRaw(), _imagePreDecimation),
							util2d::decimate(decimatedData.depthOrRightRaw(), _imagePreDecimation),
							stereoModel);
				}
			}

			UINFO("Extract features");
			cv::Mat imageMono;
			if(decimatedData.imageRaw().channels() == 3)
			{
				cv::cvtColor(decimatedData.imageRaw(), imageMono, CV_BGR2GRAY);
			}
			else
			{
				imageMono = decimatedData.imageRaw();
			}

			cv::Mat depthMask;
			if(imagesRectified && !decimatedData.depthRaw().empty() && _depthAsMask)
			{
				if(imageMono.rows % decimatedData.depthRaw().rows == 0 &&
					imageMono.cols % decimatedData.depthRaw().cols == 0 &&
					imageMono.rows/decimatedData.depthRaw().rows == imageMono.cols/decimatedData.depthRaw().cols)
				{
					depthMask = util2d::interpolate(decimatedData.depthRaw(), imageMono.rows/decimatedData.depthRaw().rows, 0.1f);
				}
			}

			int oldMaxFeatures = _feature2D->getMaxFeatures();
			UDEBUG("rawDescriptorsKept=%d, pose=%d, maxFeatures=%d, visMaxFeatures=%d", _rawDescriptorsKept?1:0, pose.isNull()?0:1, _feature2D->getMaxFeatures(), _visMaxFeatures);
			ParametersMap tmpMaxFeatureParameter;
			if(_rawDescriptorsKept&&!pose.isNull()&&_feature2D->getMaxFeatures()>0&&_feature2D->getMaxFeatures()<_visMaxFeatures)
			{
				// The total extracted features should match the number of features used for transformation estimation
				UDEBUG("Changing temporary max features from %d to %d", _feature2D->getMaxFeatures(), _visMaxFeatures);
				tmpMaxFeatureParameter.insert(ParametersPair(Parameters::kKpMaxFeatures(), uNumber2Str(_visMaxFeatures)));
				_feature2D->parseParameters(tmpMaxFeatureParameter);
			}

			keypoints = _feature2D->generateKeypoints(
					imageMono,
					depthMask);

			if(tmpMaxFeatureParameter.size())
			{
				tmpMaxFeatureParameter.at(Parameters::kKpMaxFeatures()) = uNumber2Str(oldMaxFeatures);
				_feature2D->parseParameters(tmpMaxFeatureParameter); // reset back
			}
			t = timer.ticks();
			if(stats) stats->addStatistic(Statistics::kTimingMemKeypoints_detection(), t*1000.0f);
			UDEBUG("time keypoints (%d) = %fs", (int)keypoints.size(), t);

			descriptors = _feature2D->generateDescriptors(imageMono, keypoints);
			t = timer.ticks();
			if(stats) stats->addStatistic(Statistics::kTimingMemDescriptors_extraction(), t*1000.0f);
			UDEBUG("time descriptors (%d) = %fs", descriptors.rows, t);

			UDEBUG("ratio=%f, meanWordsPerLocation=%d", _badSignRatio, meanWordsPerLocation);
			if(descriptors.rows && descriptors.rows < _badSignRatio * float(meanWordsPerLocation))
			{
				descriptors = cv::Mat();
			}
			else
			{
				if(!imagesRectified && decimatedData.cameraModels().size())
				{
					std::vector<cv::KeyPoint> keypointsValid;
					keypointsValid.reserve(keypoints.size());
					cv::Mat descriptorsValid;
					descriptorsValid.reserve(descriptors.rows);

					//undistort keypoints before projection (RGB-D)
					if(decimatedData.cameraModels().size() == 1)
					{
						std::vector<cv::Point2f> pointsIn, pointsOut;
						cv::KeyPoint::convert(keypoints,pointsIn);
						if(decimatedData.cameraModels()[0].D_raw().cols == 6)
						{
#if CV_MAJOR_VERSION > 2 or (CV_MAJOR_VERSION == 2 and (CV_MINOR_VERSION >4 or (CV_MINOR_VERSION == 4 and CV_SUBMINOR_VERSION >=10)))
							// Equidistant / FishEye
							// get only k parameters (k1,k2,p1,p2,k3,k4)
							cv::Mat D(1, 4, CV_64FC1);
							D.at<double>(0,0) = decimatedData.cameraModels()[0].D_raw().at<double>(0,1);
							D.at<double>(0,1) = decimatedData.cameraModels()[0].D_raw().at<double>(0,1);
							D.at<double>(0,2) = decimatedData.cameraModels()[0].D_raw().at<double>(0,4);
							D.at<double>(0,3) = decimatedData.cameraModels()[0].D_raw().at<double>(0,5);
							cv::fisheye::undistortPoints(pointsIn, pointsOut,
									decimatedData.cameraModels()[0].K_raw(),
									D,
									decimatedData.cameraModels()[0].R(),
									decimatedData.cameraModels()[0].P());
						}
						else
#else
							UWARN("Too old opencv version (%d,%d,%d) to support fisheye model (min 2.4.10 required)!",
									CV_MAJOR_VERSION, CV_MINOR_VERSION, CV_SUBMINOR_VERSION);
						}
#endif
						{
							//RadialTangential
							cv::undistortPoints(pointsIn, pointsOut,
									decimatedData.cameraModels()[0].K_raw(),
									decimatedData.cameraModels()[0].D_raw(),
									decimatedData.cameraModels()[0].R(),
									decimatedData.cameraModels()[0].P());
						}
						UASSERT(pointsOut.size() == keypoints.size());
						for(unsigned int i=0; i<pointsOut.size(); ++i)
						{
							if(pointsOut.at(i).x>=0 && pointsOut.at(i).x<decimatedData.cameraModels()[0].imageWidth() &&
							   pointsOut.at(i).y>=0 && pointsOut.at(i).y<decimatedData.cameraModels()[0].imageHeight())
							{
								keypointsValid.push_back(keypoints.at(i));
								keypointsValid.back().pt.x = pointsOut.at(i).x;
								keypointsValid.back().pt.y = pointsOut.at(i).y;
								descriptorsValid.push_back(descriptors.row(i));
							}
						}
					}
					else
					{
						UASSERT(int((decimatedData.imageRaw().cols/decimatedData.cameraModels().size())*decimatedData.cameraModels().size()) == decimatedData.imageRaw().cols);
						float subImageWidth = decimatedData.imageRaw().cols/decimatedData.cameraModels().size();
						for(unsigned int i=0; i<keypoints.size(); ++i)
						{
							int cameraIndex = int(keypoints.at(i).pt.x / subImageWidth);
							UASSERT_MSG(cameraIndex >= 0 && cameraIndex < (int)decimatedData.cameraModels().size(),
									uFormat("cameraIndex=%d, models=%d, kpt.x=%f, subImageWidth=%f (Camera model image width=%d)",
											cameraIndex, (int)decimatedData.cameraModels().size(), keypoints[i].pt.x, subImageWidth, decimatedData.cameraModels()[0].imageWidth()).c_str());

							std::vector<cv::Point2f> pointsIn, pointsOut;
							pointsIn.push_back(cv::Point2f(keypoints.at(i).pt.x-subImageWidth*cameraIndex, keypoints.at(i).pt.y));
							if(decimatedData.cameraModels()[cameraIndex].D_raw().cols == 6)
							{
#if CV_MAJOR_VERSION > 2 or (CV_MAJOR_VERSION == 2 and (CV_MINOR_VERSION >4 or (CV_MINOR_VERSION == 4 and CV_SUBMINOR_VERSION >=10)))
								// Equidistant / FishEye
								// get only k parameters (k1,k2,p1,p2,k3,k4)
								cv::Mat D(1, 4, CV_64FC1);
								D.at<double>(0,0) = decimatedData.cameraModels()[cameraIndex].D_raw().at<double>(0,1);
								D.at<double>(0,1) = decimatedData.cameraModels()[cameraIndex].D_raw().at<double>(0,1);
								D.at<double>(0,2) = decimatedData.cameraModels()[cameraIndex].D_raw().at<double>(0,4);
								D.at<double>(0,3) = decimatedData.cameraModels()[cameraIndex].D_raw().at<double>(0,5);
								cv::fisheye::undistortPoints(pointsIn, pointsOut,
										decimatedData.cameraModels()[cameraIndex].K_raw(),
										D,
										decimatedData.cameraModels()[cameraIndex].R(),
										decimatedData.cameraModels()[cameraIndex].P());
							}
							else
#else
								UWARN("Too old opencv version (%d,%d,%d) to support fisheye model (min 2.4.10 required)!",
										CV_MAJOR_VERSION, CV_MINOR_VERSION, CV_SUBMINOR_VERSION);
							}
#endif
							{
								//RadialTangential
								cv::undistortPoints(pointsIn, pointsOut,
										decimatedData.cameraModels()[cameraIndex].K_raw(),
										decimatedData.cameraModels()[cameraIndex].D_raw(),
										decimatedData.cameraModels()[cameraIndex].R(),
										decimatedData.cameraModels()[cameraIndex].P());
							}

							if(pointsOut[0].x>=0 && pointsOut[0].x<decimatedData.cameraModels()[cameraIndex].imageWidth() &&
							   pointsOut[0].y>=0 && pointsOut[0].y<decimatedData.cameraModels()[cameraIndex].imageHeight())
							{
								keypointsValid.push_back(keypoints.at(i));
								keypointsValid.back().pt.x = pointsOut[0].x + subImageWidth*cameraIndex;
								keypointsValid.back().pt.y = pointsOut[0].y;
								descriptorsValid.push_back(descriptors.row(i));
							}
						}
					}

					keypoints = keypointsValid;
					descriptors = descriptorsValid;

					t = timer.ticks();
					if(stats) stats->addStatistic(Statistics::kTimingMemRectification(), t*1000.0f);
					UDEBUG("time rectification = %fs", t);
				}

				if((!decimatedData.depthRaw().empty() && decimatedData.cameraModels().size() && decimatedData.cameraModels()[0].isValidForProjection()) ||
				   (!decimatedData.rightRaw().empty() && decimatedData.stereoCameraModel().isValidForProjection()))
				{
					keypoints3D = _feature2D->generateKeypoints3D(decimatedData, keypoints);
					t = timer.ticks();
					if(stats) stats->addStatistic(Statistics::kTimingMemKeypoints_3D(), t*1000.0f);
					UDEBUG("time keypoints 3D (%d) = %fs", (int)keypoints3D.size(), t);
				}
			}
		}
		else if(data.imageRaw().empty())
		{
			UDEBUG("Empty image, cannot extract features...");
		}
		else if(_feature2D->getMaxFeatures() < 0)
		{
			UDEBUG("_feature2D->getMaxFeatures()(%d<0) so don't extract any features...", _feature2D->getMaxFeatures());
		}
		else
		{
			UDEBUG("Intermediate node detected, don't extract features!");
		}
	}
	else if(_feature2D->getMaxFeatures() >= 0 && !isIntermediateNode)
	{
		UINFO("Use odometry features: kpts=%d 3d=%d desc=%d (dim=%d, type=%d)",
				(int)data.keypoints().size(),
				(int)data.keypoints3D().size(),
				data.descriptors().rows,
				data.descriptors().cols,
				data.descriptors().type());
		keypoints = data.keypoints();
		keypoints3D = data.keypoints3D();
		descriptors = data.descriptors().clone();

		UASSERT(descriptors.empty() || descriptors.rows == (int)keypoints.size());
		UASSERT(keypoints3D.empty() || keypoints3D.size() == keypoints.size());

		int maxFeatures = _rawDescriptorsKept&&!pose.isNull()&&_feature2D->getMaxFeatures()>0&&_feature2D->getMaxFeatures()<_visMaxFeatures?_visMaxFeatures:_feature2D->getMaxFeatures();
		if((int)keypoints.size() > maxFeatures)
		{
			_feature2D->limitKeypoints(keypoints, keypoints3D, descriptors, maxFeatures);
		}
		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemKeypoints_detection(), t*1000.0f);
		UDEBUG("time keypoints (%d) = %fs", (int)keypoints.size(), t);

		if(descriptors.empty())
		{
			cv::Mat imageMono;
			if(data.imageRaw().channels() == 3)
			{
				cv::cvtColor(data.imageRaw(), imageMono, CV_BGR2GRAY);
			}
			else
			{
				imageMono = data.imageRaw();
			}

			UASSERT_MSG(imagesRectified, "Cannot extract descriptors on not rectified image from keypoints which assumed to be undistorted");
			descriptors = _feature2D->generateDescriptors(imageMono, keypoints);
		}
		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemDescriptors_extraction(), t*1000.0f);
		UDEBUG("time descriptors (%d) = %fs", descriptors.rows, t);

		if(keypoints3D.empty() &&
			((!data.depthRaw().empty() && data.cameraModels().size() && data.cameraModels()[0].isValidForProjection()) ||
		   (!data.rightRaw().empty() && data.stereoCameraModel().isValidForProjection())))
		{
			keypoints3D = _feature2D->generateKeypoints3D(data, keypoints);
			if(_feature2D->getMinDepth() > 0.0f || _feature2D->getMaxDepth() > 0.0f)
			{
				UDEBUG("");
				//remove all keypoints/descriptors with no valid 3D points
				UASSERT((int)keypoints.size() == descriptors.rows &&
						keypoints3D.size() == keypoints.size());
				std::vector<cv::KeyPoint> validKeypoints(keypoints.size());
				std::vector<cv::Point3f> validKeypoints3D(keypoints.size());
				cv::Mat validDescriptors(descriptors.size(), descriptors.type());

				int oi=0;
				for(unsigned int i=0; i<keypoints3D.size(); ++i)
				{
					if(util3d::isFinite(keypoints3D[i]))
					{
						validKeypoints[oi] = keypoints[i];
						validKeypoints3D[oi] = keypoints3D[i];
						descriptors.row(i).copyTo(validDescriptors.row(oi));
						++oi;
					}
				}
				UDEBUG("Removed %d invalid 3D points", (int)keypoints3D.size()-oi);
				validKeypoints.resize(oi);
				validKeypoints3D.resize(oi);
				keypoints = validKeypoints;
				keypoints3D = validKeypoints3D;
				descriptors = validDescriptors.rowRange(0, oi).clone();
			}
		}
		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemKeypoints_3D(), t*1000.0f);
		UDEBUG("time keypoints 3D (%d) = %fs", (int)keypoints3D.size(), t);

		UDEBUG("ratio=%f, meanWordsPerLocation=%d", _badSignRatio, meanWordsPerLocation);
		if(descriptors.rows && descriptors.rows < _badSignRatio * float(meanWordsPerLocation))
		{
			descriptors = cv::Mat();
		}
	}

	if(_parallelized)
	{
		UDEBUG("Joining dictionary update thread...");
		preUpdateThread.join(); // Wait the dictionary to be updated
		UDEBUG("Joining dictionary update thread... thread finished!");
	}

	t = timer.ticks();
	if(stats) stats->addStatistic(Statistics::kTimingMemJoining_dictionary_update(), t*1000.0f);
	if(_parallelized)
	{
		UDEBUG("time descriptor and memory update (%d of size=%d) = %fs", descriptors.rows, descriptors.cols, t);
	}
	else
	{
		UDEBUG("time descriptor (%d of size=%d) = %fs", descriptors.rows, descriptors.cols, t);
	}

	std::list<int> wordIds;
	if(descriptors.rows)
	{
		// In case the number of features we want to do quantization is lower
		// than extracted ones (that would be used for transform estimation)
		std::vector<bool> inliers;
		cv::Mat descriptorsForQuantization = descriptors;
		std::vector<int> quantizedToRawIndices;
		if(_feature2D->getMaxFeatures()>0 && descriptors.rows > _feature2D->getMaxFeatures())
		{
			UASSERT((int)keypoints.size() == descriptors.rows);
			Feature2D::limitKeypoints(keypoints, inliers, _feature2D->getMaxFeatures());

			descriptorsForQuantization = cv::Mat(_feature2D->getMaxFeatures(), descriptors.cols, descriptors.type());
			quantizedToRawIndices.resize(_feature2D->getMaxFeatures());
			unsigned int oi=0;
			UASSERT((int)inliers.size() == descriptors.rows);
			for(int k=0; k < descriptors.rows; ++k)
			{
				if(inliers[k])
				{
					UASSERT(oi < quantizedToRawIndices.size());
					if(descriptors.type() == CV_32FC1)
					{
						memcpy(descriptorsForQuantization.ptr<float>(oi), descriptors.ptr<float>(k), descriptors.cols*sizeof(float));
					}
					else
					{
						memcpy(descriptorsForQuantization.ptr<char>(oi), descriptors.ptr<char>(k), descriptors.cols*sizeof(char));
					}
					quantizedToRawIndices[oi] = k;
					++oi;
				}
			}
			UASSERT((int)oi == _feature2D->getMaxFeatures());
		}

		// Quantization to vocabulary
		wordIds = _vwd->addNewWords(descriptorsForQuantization, id);

		// Set ID -1 to features not used for quantization
		if(wordIds.size() < keypoints.size())
		{
			std::vector<int> allWordIds;
			allWordIds.resize(keypoints.size(),-1);
			int i=0;
			for(std::list<int>::iterator iter=wordIds.begin(); iter!=wordIds.end(); ++iter)
			{
				allWordIds[quantizedToRawIndices[i]] = *iter;
				++i;
			}
			int negIndex = -1;
			for(i=0; i<(int)allWordIds.size(); ++i)
			{
				if(allWordIds[i] < 0)
				{
					allWordIds[i] = negIndex--;
				}
			}
			wordIds = uVectorToList(allWordIds);
		}

		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemAdd_new_words(), t*1000.0f);
		UDEBUG("time addNewWords %fs indexed=%d not=%d", t, _vwd->getIndexedWordsCount(), _vwd->getNotIndexedWordsCount());
	}
	else if(id>0)
	{
		UDEBUG("id %d is a bad signature", id);
	}

	std::multimap<int, cv::KeyPoint> words;
	std::multimap<int, cv::Point3f> words3D;
	std::multimap<int, cv::Mat> wordsDescriptors;
	if(wordIds.size() > 0)
	{
		UASSERT(wordIds.size() == keypoints.size());
		UASSERT(keypoints3D.size() == 0 || keypoints3D.size() == wordIds.size());
		unsigned int i=0;
		float decimationRatio = float(preDecimation) / float(_imagePostDecimation);
		double log2value = log(double(preDecimation))/log(2.0);
		for(std::list<int>::iterator iter=wordIds.begin(); iter!=wordIds.end() && i < keypoints.size(); ++iter, ++i)
		{
			cv::KeyPoint kpt = keypoints[i];
			if(preDecimation != _imagePostDecimation)
			{
				// remap keypoints to final image size
				kpt.pt.x *= decimationRatio;
				kpt.pt.y *= decimationRatio;
				kpt.size *= decimationRatio;
				kpt.octave += log2value;
			}
			words.insert(std::pair<int, cv::KeyPoint>(*iter, kpt));

			if(keypoints3D.size())
			{
				words3D.insert(std::pair<int, cv::Point3f>(*iter, keypoints3D.at(i)));
			}
			if(_rawDescriptorsKept)
			{
				wordsDescriptors.insert(std::pair<int, cv::Mat>(*iter, descriptors.row(i).clone()));
			}
		}
	}

	Landmarks landmarks = data.landmarks();
	if(_detectMarkers)
	{
		UDEBUG("Detecting markers...");
		if(landmarks.empty())
		{
			std::map<int, Transform> markers;
			if(!data.cameraModels().empty() && data.cameraModels()[0].isValidForProjection())
			{
				if(data.cameraModels().size() > 1)
				{
					static bool warned = false;
					if(!warned)
					{
						UWARN("Detecting markers in multi-camera setup is not yet implemented, aborting marker detection. This message is only printed once.");
					}
					warned = true;
				}
				else
				{
					markers = _markerDetector->detect(data.imageRaw(), data.cameraModels()[0], data.depthRaw());
				}
			}
			else if(data.stereoCameraModel().isValidForProjection())
			{
				markers = _markerDetector->detect(data.imageRaw(), data.stereoCameraModel().left());
			}
			for(std::map<int, Transform>::iterator iter=markers.begin(); iter!=markers.end(); ++iter)
			{
				if(iter->first <= 0)
				{
					UERROR("Invalid marker received! IDs should be > 0 (it is %d). Ignoring this marker.", iter->first);
					continue;
				}
				cv::Mat covariance = cv::Mat::eye(6,6,CV_64FC1);
				covariance(cv::Range(0,3), cv::Range(0,3)) *= _markerLinVariance;
				covariance(cv::Range(3,6), cv::Range(3,6)) *= _markerAngVariance;
				landmarks.insert(std::make_pair(iter->first, Landmark(iter->first, iter->second, covariance)));
			}
			UDEBUG("Markers detected = %d", (int)markers.size());
		}
		else
		{
			UWARN("Input data has already landmarks, cannot do marker detection.");
		}
		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemMarkers_detection(), t*1000.0f);
		UDEBUG("time markers detection = %fs", t);
	}

	cv::Mat image = data.imageRaw();
	cv::Mat depthOrRightImage = data.depthOrRightRaw();
	std::vector<CameraModel> cameraModels = data.cameraModels();
	StereoCameraModel stereoCameraModel = data.stereoCameraModel();

	// apply decimation?
	if(_imagePostDecimation > 1 && !isIntermediateNode)
	{
		if(_imagePostDecimation == preDecimation && decimatedData.isValid())
		{
			image = decimatedData.imageRaw();
			depthOrRightImage = decimatedData.depthOrRightRaw();
			cameraModels = decimatedData.cameraModels();
			stereoCameraModel = decimatedData.stereoCameraModel();
		}
		else
		{
			if(!data.rightRaw().empty() ||
				(data.depthRaw().rows == image.rows && data.depthRaw().cols == image.cols))
			{
				depthOrRightImage = util2d::decimate(depthOrRightImage, _imagePostDecimation);
			}
			image = util2d::decimate(image, _imagePostDecimation);
			for(unsigned int i=0; i<cameraModels.size(); ++i)
			{
				cameraModels[i] = cameraModels[i].scaled(1.0/double(_imagePostDecimation));
			}
			if(stereoCameraModel.isValidForProjection())
			{
				stereoCameraModel.scale(1.0/double(_imagePostDecimation));
			}
		}

		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemPost_decimation(), t*1000.0f);
		UDEBUG("time post-decimation = %fs", t);
	}

	bool triangulateWordsWithoutDepth = !_depthAsMask;
	if(!pose.isNull() &&
		cameraModels.size() == 1 &&
		words.size() &&
		(words3D.size() == 0 || (triangulateWordsWithoutDepth && words.size() == words3D.size())) &&
		_registrationPipeline->isImageRequired() &&
		_signatures.size() &&
		_signatures.rbegin()->second->mapId() == _idMapCount) // same map
	{
		UDEBUG("Generate 3D words using odometry");
		Signature * previousS = _signatures.rbegin()->second;
		if(previousS->getWords().size() > 8 && words.size() > 8 && !previousS->getPose().isNull())
		{
			UDEBUG("Previous pose(%d) = %s", previousS->id(), previousS->getPose().prettyPrint().c_str());
			UDEBUG("Current pose(%d) = %s", id, pose.prettyPrint().c_str());
			Transform cameraTransform = pose.inverse() * previousS->getPose();

			Signature cpPrevious(2);
			// IDs should be unique so that registration doesn't override them
			std::map<int, cv::KeyPoint> uniqueWords = uMultimapToMapUnique(previousS->getWords());
			std::map<int, cv::Mat> uniqueWordsDescriptors = uMultimapToMapUnique(previousS->getWordsDescriptors());
			cpPrevious.sensorData().setCameraModels(previousS->sensorData().cameraModels());
			cpPrevious.setWords(std::multimap<int, cv::KeyPoint>(uniqueWords.begin(), uniqueWords.end()));
			cpPrevious.setWordsDescriptors(std::multimap<int, cv::Mat>(uniqueWordsDescriptors.begin(), uniqueWordsDescriptors.end()));
			Signature cpCurrent(1);
			uniqueWords = uMultimapToMapUnique(words);
			uniqueWordsDescriptors = uMultimapToMapUnique(wordsDescriptors);
			cpCurrent.sensorData().setCameraModels(cameraModels);
			cpCurrent.setWords(std::multimap<int, cv::KeyPoint>(uniqueWords.begin(), uniqueWords.end()));
			cpCurrent.setWordsDescriptors(std::multimap<int, cv::Mat>(uniqueWordsDescriptors.begin(), uniqueWordsDescriptors.end()));
			// This will force comparing descriptors between both images directly
			Transform tmpt = _registrationPipeline->computeTransformationMod(cpCurrent, cpPrevious, cameraTransform);
			UDEBUG("t=%s", tmpt.prettyPrint().c_str());

			// compute 3D words by epipolar geometry with the previous signature
			std::map<int, cv::Point3f> inliers = util3d::generateWords3DMono(
					uMultimapToMapUnique(cpCurrent.getWords()),
					uMultimapToMapUnique(cpPrevious.getWords()),
					cameraModels[0],
					cameraTransform);

			UDEBUG("inliers=%d", (int)inliers.size());

			// words3D should have the same size than words if not empty
			float bad_point = std::numeric_limits<float>::quiet_NaN ();
			UASSERT(words3D.size() == 0 || words.size() == words3D.size());
			bool words3DWasEmpty = words3D.empty();
			int added3DPointsWithoutDepth = 0;
			for(std::multimap<int, cv::KeyPoint>::const_iterator iter=words.begin(); iter!=words.end(); ++iter)
			{
				std::map<int, cv::Point3f>::iterator jter=inliers.find(iter->first);
				std::multimap<int, cv::Point3f>::iterator iter3D = words3D.find(iter->first);
				if(iter3D == words3D.end())
				{
					if(jter != inliers.end())
					{
						words3D.insert(std::make_pair(iter->first, jter->second));
						++added3DPointsWithoutDepth;
					}
					else
					{
						words3D.insert(std::make_pair(iter->first, cv::Point3f(bad_point,bad_point,bad_point)));
					}
				}
				else if(!util3d::isFinite(iter3D->second) && jter != inliers.end())
				{
					iter3D->second = jter->second;
					++added3DPointsWithoutDepth;
				}
				else if(words3DWasEmpty && jter == inliers.end())
				{
					// duplicate
					words3D.insert(std::make_pair(iter->first, cv::Point3f(bad_point,bad_point,bad_point)));
				}
			}
			UDEBUG("added3DPointsWithoutDepth=%d", added3DPointsWithoutDepth);
			if(stats) stats->addStatistic(Statistics::kMemoryTriangulated_points(), (float)added3DPointsWithoutDepth);

			t = timer.ticks();
			UASSERT(words3D.size() == words.size());
			if(stats) stats->addStatistic(Statistics::kTimingMemKeypoints_3D_motion(), t*1000.0f);
			UDEBUG("time keypoints 3D by motion (%d) = %fs", (int)words3D.size(), t);
		}
	}

	// Filter the laser scan?
	LaserScan laserScan = data.laserScanRaw();
	if(!isIntermediateNode && laserScan.size())
	{
		if(laserScan.rangeMax() == 0.0f)
		{
			bool id2d = laserScan.is2d();
			float maxRange = 0.0f;
			for(int i=0; i<laserScan.size(); ++i)
			{
				const float * ptr = laserScan.data().ptr<float>(0, i);
				float r;
				if(id2d)
				{
					r = ptr[0]*ptr[0] + ptr[1]*ptr[1];
				}
				else
				{
					r = ptr[0]*ptr[0] + ptr[1]*ptr[1] + ptr[2]*ptr[2];
				}
				if(r>maxRange)
				{
					maxRange = r;
				}
			}
			if(maxRange > 0.0f)
			{
				laserScan=LaserScan(laserScan.data(), laserScan.maxPoints(), sqrt(maxRange), laserScan.format(), laserScan.localTransform());
			}
		}

		laserScan = util3d::commonFiltering(laserScan,
				_laserScanDownsampleStepSize,
				0,
				0,
				_laserScanVoxelSize,
				_laserScanNormalK,
				_laserScanNormalRadius);
		t = timer.ticks();
		if(stats) stats->addStatistic(Statistics::kTimingMemScan_filtering(), t*1000.0f);
		UDEBUG("time normals scan = %fs", t);
	}

	Signature * s;
	if(this->isBinDataKept() && (!isIntermediateNode || _saveIntermediateNodeData))
	{
		UDEBUG("Bin data kept: rgb=%d, depth=%d, scan=%d, userData=%d",
				image.empty()?0:1,
				depthOrRightImage.empty()?0:1,
				laserScan.isEmpty()?0:1,
				data.userDataRaw().empty()?0:1);

		std::vector<unsigned char> imageBytes;
		std::vector<unsigned char> depthBytes;

		if(_saveDepth16Format && !depthOrRightImage.empty() && depthOrRightImage.type() == CV_32FC1)
		{
			UWARN("Save depth data to 16 bits format: depth type detected is 32FC1, use 16UC1 depth format to avoid this conversion (or set parameter \"Mem/SaveDepth16Format\"=false to use 32bits format).");
			depthOrRightImage = util2d::cvtDepthFromFloat(depthOrRightImage);
		}

		cv::Mat compressedImage;
		cv::Mat compressedDepth;
		cv::Mat compressedScan;
		cv::Mat compressedUserData;
		if(_compressionParallelized)
		{
			rtabmap::CompressionThread ctImage(image, _rgbCompressionFormat);
			rtabmap::CompressionThread ctDepth(depthOrRightImage, depthOrRightImage.type() == CV_32FC1 || depthOrRightImage.type() == CV_16UC1?std::string(".png"):_rgbCompressionFormat);
			rtabmap::CompressionThread ctLaserScan(laserScan.data());
			rtabmap::CompressionThread ctUserData(data.userDataRaw());
			if(!image.empty())
			{
				ctImage.start();
			}
			if(!depthOrRightImage.empty())
			{
				ctDepth.start();
			}
			if(!laserScan.isEmpty())
			{
				ctLaserScan.start();
			}
			if(!data.userDataRaw().empty())
			{
				ctUserData.start();
			}
			ctImage.join();
			ctDepth.join();
			ctLaserScan.join();
			ctUserData.join();

			compressedImage = ctImage.getCompressedData();
			compressedDepth = ctDepth.getCompressedData();
			compressedScan = ctLaserScan.getCompressedData();
			compressedUserData = ctUserData.getCompressedData();
		}
		else
		{
			compressedImage = compressImage2(image, _rgbCompressionFormat);
			compressedDepth = compressImage2(depthOrRightImage, depthOrRightImage.type() == CV_32FC1 || depthOrRightImage.type() == CV_16UC1?std::string(".png"):_rgbCompressionFormat);
			compressedScan = compressData2(laserScan.data());
			compressedUserData = compressData2(data.userDataRaw());
		}

		s = new Signature(id,
			_idMapCount,
			isIntermediateNode?-1:0, // tag intermediate nodes as weight=-1
			data.stamp(),
			"",
			pose,
			data.groundTruth(),
			stereoCameraModel.isValidForProjection()?
				SensorData(
						laserScan.angleIncrement() == 0.0f?
							LaserScan(compressedScan,
								laserScan.maxPoints(),
								laserScan.rangeMax(),
								laserScan.format(),
								laserScan.localTransform()):
							LaserScan(compressedScan,
								laserScan.format(),
								laserScan.rangeMin(),
								laserScan.rangeMax(),
								laserScan.angleMin(),
								laserScan.angleMax(),
								laserScan.angleIncrement(),
								laserScan.localTransform()),
						compressedImage,
						compressedDepth,
						stereoCameraModel,
						id,
						0,
						compressedUserData):
				SensorData(
						laserScan.angleIncrement() == 0.0f?
							LaserScan(compressedScan,
								laserScan.maxPoints(),
								laserScan.rangeMax(),
								laserScan.format(),
								laserScan.localTransform()):
							LaserScan(compressedScan,
								laserScan.format(),
								laserScan.rangeMin(),
								laserScan.rangeMax(),
								laserScan.angleMin(),
								laserScan.angleMax(),
								laserScan.angleIncrement(),
								laserScan.localTransform()),
						compressedImage,
						compressedDepth,
						cameraModels,
						id,
						0,
						compressedUserData));
	}
	else
	{
		UDEBUG("Bin data kept: scan=%d, userData=%d",
						laserScan.isEmpty()?0:1,
						data.userDataRaw().empty()?0:1);

		// just compress user data and laser scan (scans can be used for local scan matching)
		cv::Mat compressedScan;
		cv::Mat compressedUserData;
		if(_compressionParallelized)
		{
			rtabmap::CompressionThread ctUserData(data.userDataRaw());
			rtabmap::CompressionThread ctLaserScan(laserScan.data());
			if(!data.userDataRaw().empty() && !isIntermediateNode)
			{
				ctUserData.start();
			}
			if(!laserScan.isEmpty() && !isIntermediateNode)
			{
				ctLaserScan.start();
			}
			ctUserData.join();
			ctLaserScan.join();

			compressedScan = ctLaserScan.getCompressedData();
			compressedUserData = ctUserData.getCompressedData();
		}
		else
		{
			compressedScan = compressData2(laserScan.data());
			compressedUserData = compressData2(data.userDataRaw());
		}

		s = new Signature(id,
			_idMapCount,
			isIntermediateNode?-1:0, // tag intermediate nodes as weight=-1
			data.stamp(),
			"",
			pose,
			data.groundTruth(),
			stereoCameraModel.isValidForProjection()?
				SensorData(
						laserScan.angleIncrement() == 0.0f?
								LaserScan(compressedScan,
									laserScan.maxPoints(),
									laserScan.rangeMax(),
									laserScan.format(),
									laserScan.localTransform()):
								LaserScan(compressedScan,
									laserScan.format(),
									laserScan.rangeMin(),
									laserScan.rangeMax(),
									laserScan.angleMin(),
									laserScan.angleMax(),
									laserScan.angleIncrement(),
									laserScan.localTransform()),
						cv::Mat(),
						cv::Mat(),
						stereoCameraModel,
						id,
						0,
						compressedUserData):
				SensorData(
						laserScan.angleIncrement() == 0.0f?
								LaserScan(compressedScan,
									laserScan.maxPoints(),
									laserScan.rangeMax(),
									laserScan.format(),
									laserScan.localTransform()):
								LaserScan(compressedScan,
									laserScan.format(),
									laserScan.rangeMin(),
									laserScan.rangeMax(),
									laserScan.angleMin(),
									laserScan.angleMax(),
									laserScan.angleIncrement(),
									laserScan.localTransform()),
						cv::Mat(),
						cv::Mat(),
						cameraModels,
						id,
						0,
						compressedUserData));
	}

	s->setWords(words);
	s->setWords3(words3D);
	s->setWordsDescriptors(wordsDescriptors);

	// set raw data
	if(!cameraModels.empty())
	{
		s->sensorData().setRGBDImage(image, depthOrRightImage, cameraModels, false);
	}
	else
	{
		s->sensorData().setStereoImage(image, depthOrRightImage, stereoCameraModel, false);
	}
	s->sensorData().setLaserScan(laserScan, false);
	s->sensorData().setUserData(data.userDataRaw(), false);

	s->sensorData().setGroundTruth(data.groundTruth());
	s->sensorData().setGPS(data.gps());
	s->sensorData().setEnvSensors(data.envSensors());

	t = timer.ticks();
	if(stats) stats->addStatistic(Statistics::kTimingMemCompressing_data(), t*1000.0f);
	UDEBUG("time compressing data (id=%d) %fs", id, t);
	if(words.size())
	{
		s->setEnabled(true); // All references are already activated in the dictionary at this point (see _vwd->addNewWords())
	}

	// Occupancy grid map stuff
	if(_createOccupancyGrid && !isIntermediateNode)
	{
		if(!data.depthOrRightRaw().empty())
		{
			cv::Mat ground, obstacles, empty;
			float cellSize = 0.0f;
			cv::Point3f viewPoint(0,0,0);
			_occupancy->createLocalMap(*s, ground, obstacles, empty, viewPoint);
			cellSize = _occupancy->getCellSize();
			s->sensorData().setOccupancyGrid(ground, obstacles, empty, cellSize, viewPoint);

			t = timer.ticks();
			if(stats) stats->addStatistic(Statistics::kTimingMemOccupancy_grid(), t*1000.0f);
			UDEBUG("time grid map = %fs", t);
		}
		else if(data.gridCellSize() != 0.0f)
		{
			s->sensorData().setOccupancyGrid(
					data.gridGroundCellsRaw(),
					data.gridObstacleCellsRaw(),
					data.gridEmptyCellsRaw(),
					data.gridCellSize(),
					data.gridViewPoint());
		}
	}

	// prior
	if(!isIntermediateNode)
	{
		if(!data.globalPose().isNull() && data.globalPoseCovariance().cols==6 && data.globalPoseCovariance().rows==6 && data.globalPoseCovariance().cols==CV_64FC1)
		{
			s->addLink(Link(s->id(), s->id(), Link::kPosePrior, data.globalPose(), data.globalPoseCovariance().inv()));
			UDEBUG("Added global pose prior: %s", data.globalPose().prettyPrint().c_str());

			if(data.gps().stamp() > 0.0)
			{
				UWARN("GPS constraint ignored as global pose is also set.");
			}
		}
		else if(data.gps().stamp() > 0.0)
		{
			if(_gpsOrigin.stamp() <= 0.0)
			{
				_gpsOrigin =  data.gps();
				UINFO("Added GPS origin: long=%f lat=%f alt=%f bearing=%f error=%f", data.gps().longitude(), data.gps().latitude(), data.gps().altitude(), data.gps().bearing(), data.gps().error());
			}
			cv::Point3f pt = data.gps().toGeodeticCoords().toENU_WGS84(_gpsOrigin.toGeodeticCoords());
			Transform gpsPose(pt.x, pt.y, pose.z(), 0, 0, -(data.gps().bearing()-90.0)*180.0/M_PI);
			cv::Mat gpsInfMatrix = cv::Mat::eye(6,6,CV_64FC1)/9999.0; // variance not used >= 9999
			if(data.gps().error() > 0.0)
			{
				UDEBUG("Added GPS prior: x=%f y=%f z=%f yaw=%f", gpsPose.x(), gpsPose.y(), gpsPose.z(), gpsPose.theta());
				// only set x, y as we don't know variance for other degrees of freedom.
				gpsInfMatrix.at<double>(0,0) = gpsInfMatrix.at<double>(1,1) = 1.0/data.gps().error();
				gpsInfMatrix.at<double>(2,2) = 1; // z variance is set to avoid issues with g2o and gtsam requiring a prior on Z
				s->addLink(Link(s->id(), s->id(), Link::kPosePrior, gpsPose, gpsInfMatrix));
			}
			else
			{
				UERROR("Invalid GPS error value (%f m), must be > 0 m.", data.gps().error());
			}
		}
	}

	//landmarks
	for(Landmarks::const_iterator iter = landmarks.begin(); iter!=landmarks.end(); ++iter)
	{
		if(iter->second.id() > 0)
		{
			int landmarkId = -iter->first;
			Link landmark(s->id(), landmarkId, Link::kLandmark, iter->second.pose(), iter->second.covariance().inv());
			s->addLandmark(landmark);

			// Update landmark indexes
			std::map<int, std::set<int> >::iterator nter = _landmarksIndex.find(s->id());
			if(nter!=_landmarksIndex.end())
			{
				nter->second.insert(landmarkId);
			}
			else
			{
				std::set<int> tmp;
				tmp.insert(landmarkId);
				_landmarksIndex.insert(std::make_pair(s->id(), tmp));
			}
			nter = _landmarksInvertedIndex.find(landmarkId);
			if(nter!=_landmarksInvertedIndex.end())
			{
				nter->second.insert(s->id());
			}
			else
			{
				std::set<int> tmp;
				tmp.insert(s->id());
				_landmarksInvertedIndex.insert(std::make_pair(landmarkId, tmp));
			}
		}
		else
		{
			UERROR("Invalid landmark received! IDs should be > 0 (it is %d). Ignoring this landmark.", iter->second.id());
		}
	}

	return s;
}

void Memory::disableWordsRef(int signatureId)
{
	UDEBUG("id=%d", signatureId);

	Signature * ss = this->_getSignature(signatureId);
	if(ss && ss->isEnabled())
	{
		const std::multimap<int, cv::KeyPoint> & words = ss->getWords();
		const std::list<int> & keys = uUniqueKeys(words);
		int count = _vwd->getTotalActiveReferences();
		// First remove all references
		for(std::list<int>::const_iterator i=keys.begin(); i!=keys.end(); ++i)
		{
			_vwd->removeAllWordRef(*i, signatureId);
		}

		count -= _vwd->getTotalActiveReferences();
		ss->setEnabled(false);
		UDEBUG("%d words total ref removed from signature %d... (total active ref = %d)", count, ss->id(), _vwd->getTotalActiveReferences());
	}
}

void Memory::cleanUnusedWords()
{
	std::vector<VisualWord*> removedWords = _vwd->getUnusedWords();
	UDEBUG("Removing %d words (dictionary size=%d)...", removedWords.size(), _vwd->getVisualWords().size());
	if(removedWords.size())
	{
		// remove them from the dictionary
		_vwd->removeWords(removedWords);

		for(unsigned int i=0; i<removedWords.size(); ++i)
		{
			if(_dbDriver)
			{
				_dbDriver->asyncSave(removedWords[i]);
			}
			else
			{
				delete removedWords[i];
			}
		}
	}
}

void Memory::enableWordsRef(const std::list<int> & signatureIds)
{
	UDEBUG("size=%d", signatureIds.size());
	UTimer timer;
	timer.start();

	std::map<int, int> refsToChange; //<oldWordId, activeWordId>

	std::set<int> oldWordIds;
	std::list<Signature *> surfSigns;
	for(std::list<int>::const_iterator i=signatureIds.begin(); i!=signatureIds.end(); ++i)
	{
		Signature * ss = dynamic_cast<Signature *>(this->_getSignature(*i));
		if(ss && !ss->isEnabled())
		{
			surfSigns.push_back(ss);
			std::list<int> uniqueKeys = uUniqueKeys(ss->getWords());

			//Find words in the signature which they are not in the current dictionary
			for(std::list<int>::const_iterator k=uniqueKeys.begin(); k!=uniqueKeys.end(); ++k)
			{
				if(*k>0 && _vwd->getWord(*k) == 0 && _vwd->getUnusedWord(*k) == 0)
				{
					oldWordIds.insert(oldWordIds.end(), *k);
				}
			}
		}
	}

	if(!_vwd->isIncremental() && oldWordIds.size())
	{
		UWARN("Dictionary is fixed, but some words retrieved have not been found!?");
	}

	UDEBUG("oldWordIds.size()=%d, getOldIds time=%fs", oldWordIds.size(), timer.ticks());

	// the words were deleted, so try to math it with an active word
	std::list<VisualWord *> vws;
	if(oldWordIds.size() && _dbDriver)
	{
		// get the descriptors
		_dbDriver->loadWords(oldWordIds, vws);
	}
	UDEBUG("loading words(%d) time=%fs", oldWordIds.size(), timer.ticks());


	if(vws.size())
	{
		//Search in the dictionary
		std::vector<int> vwActiveIds = _vwd->findNN(vws);
		UDEBUG("find active ids (number=%d) time=%fs", vws.size(), timer.ticks());
		int i=0;
		for(std::list<VisualWord *>::iterator iterVws=vws.begin(); iterVws!=vws.end(); ++iterVws)
		{
			if(vwActiveIds[i] > 0)
			{
				//UDEBUG("Match found %d with %d", (*iterVws)->id(), vwActiveIds[i]);
				refsToChange.insert(refsToChange.end(), std::pair<int, int>((*iterVws)->id(), vwActiveIds[i]));
				if((*iterVws)->isSaved())
				{
					delete (*iterVws);
				}
				else if(_dbDriver)
				{
					_dbDriver->asyncSave(*iterVws);
				}
			}
			else
			{
				//add to dictionary
				_vwd->addWord(*iterVws); // take ownership
			}
			++i;
		}
		UDEBUG("Added %d to dictionary, time=%fs", vws.size()-refsToChange.size(), timer.ticks());

		//update the global references map and update the signatures reactivated
		for(std::map<int, int>::const_iterator iter=refsToChange.begin(); iter != refsToChange.end(); ++iter)
		{
			//uInsert(_wordRefsToChange, (const std::pair<int, int>)*iter); // This will be used to change references in the database
			for(std::list<Signature *>::iterator j=surfSigns.begin(); j!=surfSigns.end(); ++j)
			{
				(*j)->changeWordsRef(iter->first, iter->second);
			}
		}
		UDEBUG("changing ref, total=%d, time=%fs", refsToChange.size(), timer.ticks());
	}

	int count = _vwd->getTotalActiveReferences();

	// Reactivate references and signatures
	for(std::list<Signature *>::iterator j=surfSigns.begin(); j!=surfSigns.end(); ++j)
	{
		const std::vector<int> & keys = uKeys((*j)->getWords());
		if(keys.size())
		{
			// Add all references
			for(unsigned int i=0; i<keys.size(); ++i)
			{
				if(keys.at(i)>0)
				{
					_vwd->addWordRef(keys.at(i), (*j)->id());
				}
			}
			(*j)->setEnabled(true);
		}
	}

	count = _vwd->getTotalActiveReferences() - count;
	UDEBUG("%d words total ref added from %d signatures, time=%fs...", count, surfSigns.size(), timer.ticks());
}

std::set<int> Memory::reactivateSignatures(const std::list<int> & ids, unsigned int maxLoaded, double & timeDbAccess)
{
	// get the signatures, if not in the working memory, they
	// will be loaded from the database in an more efficient way
	// than how it is done in the Memory

	UDEBUG("");
	UTimer timer;
	std::list<int> idsToLoad;
	std::map<int, int>::iterator wmIter;
	for(std::list<int>::const_iterator i=ids.begin(); i!=ids.end(); ++i)
	{
		if(!this->getSignature(*i) && !uContains(idsToLoad, *i))
		{
			if(!maxLoaded || idsToLoad.size() < maxLoaded)
			{
				idsToLoad.push_back(*i);
				UINFO("Loading location %d from database...", *i);
			}
		}
	}

	UDEBUG("idsToLoad = %d", idsToLoad.size());

	std::list<Signature *> reactivatedSigns;
	if(_dbDriver)
	{
		_dbDriver->loadSignatures(idsToLoad, reactivatedSigns);
	}
	timeDbAccess = timer.getElapsedTime();
	std::list<int> idsLoaded;
	for(std::list<Signature *>::iterator i=reactivatedSigns.begin(); i!=reactivatedSigns.end(); ++i)
	{
		if(!(*i)->getLandmarks().empty())
		{
			// Update landmark indexes
			for(std::map<int, Link>::const_iterator iter = (*i)->getLandmarks().begin(); iter!=(*i)->getLandmarks().end(); ++iter)
			{
				int landmarkId = iter->first;
				UASSERT(landmarkId < 0);

				std::map<int, std::set<int> >::iterator nter = _landmarksIndex.find((*i)->id());
				if(nter!=_landmarksIndex.end())
				{
					nter->second.insert(landmarkId);
				}
				else
				{
					std::set<int> tmp;
					tmp.insert(landmarkId);
					_landmarksIndex.insert(std::make_pair((*i)->id(), tmp));
				}
				nter = _landmarksInvertedIndex.find(landmarkId);
				if(nter!=_landmarksInvertedIndex.end())
				{
					nter->second.insert((*i)->id());
				}
				else
				{
					std::set<int> tmp;
					tmp.insert((*i)->id());
					_landmarksInvertedIndex.insert(std::make_pair(landmarkId, tmp));
				}
			}
		}

		idsLoaded.push_back((*i)->id());
		//append to working memory
		this->addSignatureToWmFromLTM(*i);
	}
	this->enableWordsRef(idsLoaded);
	UDEBUG("time = %fs", timer.ticks());
	return std::set<int>(idsToLoad.begin(), idsToLoad.end());
}

// return all non-null poses
// return unique links between nodes (for neighbors: old->new, for loops: parent->child)
void Memory::getMetricConstraints(
		const std::set<int> & ids,
		std::map<int, Transform> & poses,
		std::multimap<int, Link> & links,
		bool lookInDatabase,
		bool landmarksAdded)
{
	UDEBUG("");
	for(std::set<int>::const_iterator iter=ids.begin(); iter!=ids.end(); ++iter)
	{
		Transform pose = getOdomPose(*iter, lookInDatabase);
		if(!pose.isNull())
		{
			poses.insert(std::make_pair(*iter, pose));
		}
	}

	for(std::set<int>::const_iterator iter=ids.begin(); iter!=ids.end(); ++iter)
	{
		if(uContains(poses, *iter))
		{
			std::map<int, Link> tmpLinks = getLinks(*iter, lookInDatabase, true);
			for(std::map<int, Link>::iterator jter=tmpLinks.begin(); jter!=tmpLinks.end(); ++jter)
			{
				if(	jter->second.isValid() &&
					graph::findLink(links, *iter, jter->first) == links.end() &&
					(uContains(poses, jter->first) || (landmarksAdded && jter->second.type() == Link::kLandmark)))
				{
					if(!lookInDatabase &&
					   (jter->second.type() == Link::kNeighbor ||
					    jter->second.type() == Link::kNeighborMerged))
					{
						const Signature * s = this->getSignature(jter->first);
						UASSERT(s!=0);
						if(s->getWeight() == -1)
						{
							Link link = jter->second;
							while(s && s->getWeight() == -1)
							{
								// skip to next neighbor, well we assume that bad signatures
								// are only linked by max 2 neighbor links.
								std::map<int, Link> n = this->getNeighborLinks(s->id(), false);
								UASSERT(n.size() <= 2);
								std::map<int, Link>::iterator uter = n.upper_bound(s->id());
								if(uter != n.end())
								{
									const Signature * s2 = this->getSignature(uter->first);
									if(s2)
									{
										link = link.merge(uter->second, uter->second.type());
										poses.erase(s->id());
										s = s2;
									}

								}
								else
								{
									break;
								}
							}
							links.insert(std::make_pair(*iter, link));
						}
						else
						{
							links.insert(std::make_pair(*iter, jter->second));
						}
					}
					else if(jter->second.type() != Link::kLandmark)
					{
						links.insert(std::make_pair(*iter, jter->second));
					}
					else if(landmarksAdded)
					{
						if(!uContains(poses, jter->first))
						{
							poses.insert(std::make_pair(jter->first, poses.at(*iter) * jter->second.transform()));
						}
						links.insert(std::make_pair(jter->first, jter->second.inverse()));
					}
				}
			}
		}
	}
}

} // namespace rtabmap
