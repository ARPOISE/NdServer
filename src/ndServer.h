/*
 * ndServer.h - Include file for the ARpoise net distribution server.
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
#ifndef _ND_SERVER_H_
#define _ND_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ndConnection.h"
#include "pbl.h"

	typedef struct NdScene_s
	{
		char id[ND_ID_LENGTH + 1];
		char* sceneUrl;
		char* sceneName;
		PblSet* connectionSet;

	} NdScene;

	extern char** ndArguments;

	extern void ndDispatchInit();
	extern void ndDispatchExit();
	extern void ndDispatchLoop();
	extern int ndDispatchCreateListenSocket();

	extern int ndRequestHandle(NdConnection* conn);

	extern int ndSceneNofConnections(NdScene* scene);
	extern int ndSceneMapNofScenes();
	extern NdScene* tcpSceneCreate(NdConnection* conn);
	extern NdScene* ndSceneFind(char* sceneUrl);
	extern NdScene* ndSceneGet(char* sceneId);
	extern void tcpSceneClose(NdScene* scene);

#ifdef __cplusplus
}
#endif
#endif
