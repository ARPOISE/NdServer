/*
 * ndScene.c - Manage the scenes.
 *
 * Copyright (C) 2023, Tamiko Thiel and Peter Graf - All Rights Reserved
 *
 * ARpoise/NdServer - Augmented Reality point of interest service environment / Net Distribution Server
 *
 * This file is part of ARpoise.
 *
 *  ARpoise is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ARpoise is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ARpoise.  If not, see <https://www.gnu.org/licenses/>.
 *
 * For more information on
 *
 * Tamiko Thiel, see www.TamikoThiel.com/
 * Peter Graf, see www.mission-base.com/peter/
 * ARpoise, see www.ARpoise.com/
 */
#include "pblProcess.h"
#include "ndServer.h"
#include "tcpPacket.h"
#include "ndConnection.h"
#include "pbl.h"

static PblMap* _SceneMap = NULL;
static PblMap* _SceneIdMap = NULL;
unsigned long ndScenesTotal = 0;

/*
 * Return the number of connections in the scene.
 */
int ndSceneNofConnections(NdScene* scene)
{
	return scene && scene->connectionSet ? pblSetSize(scene->connectionSet) : 0;
}

/*
 * Return the number of open scenes.
 */
int ndSceneMapNofScenes()
{
	return _SceneMap ? pblMapSize(_SceneMap) : 0;
}

/*
 * Find a scene for a given scene url.
 *
 * Returns NULL if no scene is found.
 */
NdScene* ndSceneFind(char* sceneUrl)
{
	NdScene** scenePtr = (_SceneMap ? pblMapGetStr(_SceneMap, sceneUrl) : NULL);
	return scenePtr ? *scenePtr : NULL;
}

/*
 * Get a scene for a given scene id.
 *
 * Returns NULL if no scene is found.
 */
NdScene* ndSceneGet(char* sceneId)
{
	NdScene** scenePtr = (_SceneIdMap ? pblMapGetStr(_SceneIdMap, sceneId) : NULL);
	return scenePtr ? *scenePtr : NULL;
}

static unsigned int _sceneId = 0x20000;
/*
 * Create a new scene.
 *
 * Returns NULL if the scene could not be created.
 */
NdScene* tcpSceneCreate(NdConnection* conn)
{
	static char* function = "tcpSceneCreate";
	NdScene* scene = NULL;

	scene = (NdScene*)pblProcessMalloc(function, sizeof(NdScene));
	if (!scene)
	{
		LOG_ERROR(("%s: could not create scene, out of memory, pbl_errno %d.\n",
			function, pbl_errno));
		return NULL;
	}
	scene->connectionSet = pblSetNewHashSet();
	if (!scene->connectionSet)
	{
		LOG_ERROR(("%s: could not create connection set, pbl_errno %d.\n",
			function, pbl_errno));
		tcpSceneClose(scene);
		return NULL;
	}

	pbl_LongToHexString((unsigned char*)scene->id, ++_sceneId);
	scene->sceneUrl = pblProcessStrdup(function, conn->SCU);
	scene->sceneName = pblProcessStrdup(function, conn->SCN);

	if (!scene->id || !*scene->id
		|| !scene->sceneUrl || !*scene->sceneUrl
		|| !scene->sceneName || !*scene->sceneName)
	{
		LOG_ERROR(("%s: could not create scene data, out of memory, pbl_errno %d.\n",
			function, pbl_errno));
		tcpSceneClose(scene);
		return NULL;
	}

	if (!_SceneIdMap)
	{
		_SceneIdMap = pblMapNewHashMap();
	}
	if (pblMapAdd(_SceneIdMap, scene->id, 1 + strlen(scene->id), &scene, sizeof(void*)) < 0)
	{
		LOG_ERROR(("%s: could not add scene to id map, pbl_errno %d.\n",
			function, pbl_errno));
		tcpSceneClose(scene);
		return NULL;
	}
	if (!_SceneMap)
	{
		_SceneMap = pblMapNewHashMap();
	}
	if (pblMapAdd(_SceneMap, scene->sceneUrl, 1 + strlen(scene->sceneUrl), &scene, sizeof(void*)) < 0)
	{
		LOG_ERROR(("%s: could not add scene to map, pbl_errno %d.\n",
			function, pbl_errno));
		tcpSceneClose(scene);
		return NULL;
	}

	char* key = (char*)1 + conn->tcpSocket;
	if (pblSetAdd(scene->connectionSet, key) < 0)
	{
		LOG_ERROR(("%s: could not add connection to scene, pbl_errno %d.\n",
			function, pbl_errno));
		tcpSceneClose(scene);
		return NULL;
	}
	ndScenesTotal++;
	return scene;
}

/*
 * Close a scene.
 */
void tcpSceneClose(NdScene* scene)
{
	LOG_INFO(("L DEL SCEN ID %s SCU %s SCN %s\n",
		scene->id ? scene->id : "?",
		scene->sceneUrl ? scene->sceneUrl : "?",
		scene->sceneName ? scene->sceneName : "?"));

	if (_SceneIdMap && scene->id)
	{
		pblMapRemoveStr(_SceneIdMap, scene->id);
	}
	if (_SceneMap && scene->sceneUrl)
	{
		pblMapRemoveStr(_SceneMap, scene->sceneUrl);
	}

	PBL_PROCESS_FREE(scene->sceneUrl);
	PBL_PROCESS_FREE(scene->sceneName);

	if (scene->connectionSet)
	{
		pblSetFree(scene->connectionSet);
	}
	PBL_PROCESS_FREE(scene);
}
