/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2011  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/*
 * Loop.c
 *
 * The main game loop
 *
 */
#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/strres.h"
#include "lib/framework/wzapp.h"

#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h" //ivis render code
#include "lib/ivis_opengl/piemode.h"
// FIXME Direct iVis implementation include!
#include "lib/ivis_opengl/screen.h"

#include "lib/gamelib/gtime.h"
#include "lib/gamelib/animobj.h"
#include "lib/script/script.h"
#include "lib/sound/audio.h"
#include "lib/sound/cdaudio.h"
#include "lib/sound/mixer.h"
#include "lib/netplay/netplay.h"

#include "loop.h"
#include "objects.h"
#include "display.h"
#include "map.h"
#include "hci.h"
#include "ingameop.h"
#include "miscimd.h"
#include "effects.h"
#include "radar.h"
#include "projectile.h"
#include "console.h"
#include "power.h"
#include "message.h"
#include "bucket3d.h"
#include "display3d.h"
#include "warzoneconfig.h"

#include "multiplay.h" //ajl
#include "scripttabs.h"
#include "levels.h"
#include "visibility.h"
#include "multimenu.h"
#include "intelmap.h"
#include "loadsave.h"
#include "game.h"
#include "multijoin.h"
#include "lighting.h"
#include "intimage.h"
#include "lib/framework/cursors.h"
#include "seqdisp.h"
#include "mission.h"
#include "warcam.h"
#include "lighting.h"
#include "mapgrid.h"
#include "edit3d.h"
#include "drive.h"
#include "fpath.h"
#include "scriptextern.h"
#include "cluster.h"
#include "cmddroid.h"
#include "keybind.h"
#include "wrappers.h"
#include "random.h"
#include "qtscript.h"

#include "warzoneconfig.h"

#ifdef DEBUG
#include "objmem.h"
#endif

#include <numeric>


static void fireWaitingCallbacks(void);

/*
 * Global variables
 */
unsigned int loopPieCount;
unsigned int loopPolyCount;
unsigned int loopStateChanges;

/*
 * local variables
 */
static bool paused=false;
static bool video=false;

//holds which pause is valid at any one time
struct PAUSE_STATE
{
	bool gameUpdatePause;
	bool audioPause;
	bool scriptPause;
	bool scrollPause;
	bool consolePause;
	bool editPause;
};

static PAUSE_STATE	pauseState;
static	UDWORD	numDroids[MAX_PLAYERS];
static	UDWORD	numMissionDroids[MAX_PLAYERS];
static	UDWORD	numTransporterDroids[MAX_PLAYERS];
static	UDWORD	numCommandDroids[MAX_PLAYERS];
static	UDWORD	numConstructorDroids[MAX_PLAYERS];

static SDWORD videoMode = 0;

LOOP_MISSION_STATE		loopMissionState = LMS_NORMAL;

// this is set by scrStartMission to say what type of new level is to be started
SDWORD	nextMissionType = LDS_NONE;//MISSION_NONE;

 /* Force 3D display */
UDWORD	mcTime;

static GAMECODE renderLoop()
{
	if (bMultiPlayer && !NetPlay.isHostAlive && NetPlay.bComms && !NetPlay.isHost)
	{
		intAddInGamePopup();
	}

	int clearMode = 0;
	if(getDrawShadows())
	{
		clearMode |= CLEAR_SHADOW;
	}
	if (loopMissionState == LMS_SAVECONTINUE)
	{
		pie_SetFogStatus(false);
		clearMode = CLEAR_BLACK;
	}
	pie_ScreenFlip(clearMode);//gameloopflip

	HandleClosingWindows();	// Needs to be done outside the pause case.

	audio_Update();

	wzShowMouse(true);

	INT_RETVAL intRetVal = INT_NONE;
	if (!paused)
	{
		/* Run the in game interface and see if it grabbed any mouse clicks */
		if (!rotActive && getWidgetsStatus() && dragBox3D.status != DRAG_DRAGGING && wallDrag.status != DRAG_DRAGGING)
		{
			intRetVal = intRunWidgets();
		}

		//don't process the object lists if paused or about to quit to the front end
		if (!gameUpdatePaused() && intRetVal != INT_QUIT)
		{
			if( dragBox3D.status != DRAG_DRAGGING
				&& wallDrag.status != DRAG_DRAGGING
				&& ( intRetVal == INT_INTERCEPT
					|| ( radarOnScreen
						 && CoordInRadar(mouseX(), mouseY())
						 && getHQExists(selectedPlayer) ) ) )
			{
				// Using software cursors (when on) for these menus due to a bug in SDL's SDL_ShowCursor()
				wzSetCursor(CURSOR_DEFAULT);

				intRetVal = INT_INTERCEPT;
			}

#ifdef DEBUG
			// check all flag positions for duplicate delivery points
			checkFactoryFlags();
#endif

			//handles callbacks for positioning of DP's
			process3DBuilding();

			//ajl. get the incoming netgame messages and process them.
			// FIXME Previous comment is deprecated. multiPlayerLoop does some other weird stuff, but not that anymore.
			if (bMultiPlayer)
			{
				multiPlayerLoop();
			}

			for (unsigned i = 0; i < MAX_PLAYERS; i++)
			{
				for (DROID *psCurr = apsDroidLists[i]; psCurr; psCurr = psCurr->psNext)
				{
					// Don't copy the next pointer - if droids somehow get destroyed in the graphics rendering loop, who cares if we crash.
					calcDroidIllumination(psCurr);
				}
			}

			/* update animations */
			animObj_Update();
		}

		if (!consolePaused())
		{
			/* Process all the console messages */
			updateConsoleMessages();
		}
		if (!scrollPaused() && !getWarCamStatus() && dragBox3D.status != DRAG_DRAGGING && intMode != INT_INGAMEOP )
		{
			scroll();
		}
	}
	else  // paused
	{
		// Using software cursors (when on) for these menus due to a bug in SDL's SDL_ShowCursor()
		wzSetCursor(CURSOR_DEFAULT);

		if(dragBox3D.status != DRAG_DRAGGING)
		{
			scroll();
		}

		if(InGameOpUp || isInGamePopupUp)		// ingame options menu up, run it!
		{
			unsigned widgval = widgRunScreen(psWScreen);
			intProcessInGameOptions(widgval);
			if(widgval == INTINGAMEOP_QUIT_CONFIRM || widgval == INTINGAMEOP_POPUP_QUIT)
			{
				if(gamePaused())
				{
					kf_TogglePauseMode();
				}
				intRetVal = INT_QUIT;
			}
		}

		if(bLoadSaveUp && runLoadSave(true) && strlen(sRequestResult))
		{
			debug( LOG_NEVER, "Returned %s", sRequestResult );
			if(bRequestLoad)
			{
				loopMissionState = LMS_LOADGAME;
				NET_InitPlayers();			// otherwise alliances were not cleared
				sstrcpy(saveGameName, sRequestResult);
			}
			else
			{
				char msgbuffer[256]= {'\0'};

				if (saveInMissionRes())
				{
					if (saveGame(sRequestResult, GTYPE_SAVE_START))
					{
						sstrcpy(msgbuffer, _("GAME SAVED: "));
						sstrcat(msgbuffer, sRequestResult);
						addConsoleMessage( msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
					}
					else
					{
						ASSERT( false,"Mission Results: saveGame Failed" );
						sstrcpy(msgbuffer, _("Could not save game!"));
						addConsoleMessage( msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
						deleteSaveGame(sRequestResult);
					}
				}
				else if (bMultiPlayer || saveMidMission())
				{
					if (saveGame(sRequestResult, GTYPE_SAVE_MIDMISSION))//mid mission from [esc] menu
					{
						sstrcpy(msgbuffer, _("GAME SAVED: "));
						sstrcat(msgbuffer, sRequestResult);
						addConsoleMessage( msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
					}
					else
					{
						ASSERT(!"saveGame(sRequestResult, GTYPE_SAVE_MIDMISSION) failed", "Mid Mission: saveGame Failed" );
						sstrcpy(msgbuffer, _("Could not save game!"));
						addConsoleMessage( msgbuffer, LEFT_JUSTIFY, NOTIFY_MESSAGE);
						deleteSaveGame(sRequestResult);
					}
				}
				else
				{
					ASSERT( false, "Attempt to save game with incorrect load/save mode" );
				}
			}
		}
	}

	/* Check for quit */
	bool quitting = false;
	if (intRetVal == INT_QUIT)
	{
		if (!loop_GetVideoStatus())
		{
			//quitting from the game to the front end
			//so get a new backdrop
			quitting = true;

			pie_LoadBackDrop(SCREEN_RANDOMBDROP);
		}
	}
	if (!loop_GetVideoStatus() && !quitting)
	{
		if (!gameUpdatePaused())
		{
			if (dragBox3D.status != DRAG_DRAGGING
			 && wallDrag.status != DRAG_DRAGGING)
			{
				ProcessRadarInput();
			}
			processInput();

			//no key clicks or in Intelligence Screen
			if (intRetVal == INT_NONE && !InGameOpUp && !isInGamePopupUp)
			{
				processMouseClickInput();
			}
			displayWorld();
		}
		/* Display the in game interface */
		pie_SetDepthBufferStatus(DEPTH_CMP_ALWAYS_WRT_ON);
		pie_SetFogStatus(false);

		if(bMultiPlayer && bDisplayMultiJoiningStatus)
		{
			intDisplayMultiJoiningStatus(bDisplayMultiJoiningStatus);
			setWidgetsStatus(false);
		}

		if(getWidgetsStatus())
		{
			intDisplayWidgets();
		}
		pie_SetDepthBufferStatus(DEPTH_CMP_LEQ_WRT_ON);
		pie_SetFogStatus(true);
	}

	pie_GetResetCounts(&loopPieCount, &loopPolyCount, &loopStateChanges);

	if ((fogStatus & FOG_BACKGROUND) && (loopMissionState == LMS_SAVECONTINUE))
	{
		pie_SetFogStatus(false);
	}

	if (!quitting)
	{
			/* Check for toggling display mode */
			if ((keyDown(KEY_LALT) || keyDown(KEY_RALT)) && keyPressed(KEY_RETURN))
			{
				screenToggleMode();
			}
	}

	// deal with the mission state
	switch (loopMissionState)
	{
		case LMS_CLEAROBJECTS:
			missionDestroyObjects();
			setScriptPause(true);
			loopMissionState = LMS_SETUPMISSION;
			break;

		case LMS_NORMAL:
			// default
			break;
		case LMS_SETUPMISSION:
			setScriptPause(false);
			if (!setUpMission(nextMissionType))
			{
				return GAMECODE_QUITGAME;
			}
			break;
		case LMS_SAVECONTINUE:
			// just wait for this to be changed when the new mission starts
			break;
		case LMS_NEWLEVEL:
			//nextMissionType = MISSION_NONE;
			nextMissionType = LDS_NONE;
			return GAMECODE_NEWLEVEL;
			break;
		case LMS_LOADGAME:
			return GAMECODE_LOADGAME;
			break;
		default:
			ASSERT( false, "unknown loopMissionState" );
			break;
	}

	if (quitting)
	{
		pie_SetFogStatus(false);
		pie_ScreenFlip(CLEAR_BLACK);//gameloopflip
		/* Check for toggling display mode */
		if ((keyDown(KEY_LALT) || keyDown(KEY_RALT)) && keyPressed(KEY_RETURN))
		{
			screenToggleMode();
		}
		return GAMECODE_QUITGAME;
	}
	else if (loop_GetVideoStatus())
	{
		audio_StopAll();
		return GAMECODE_PLAYVIDEO;
	}

	return GAMECODE_CONTINUE;
}

static void gameStateUpdate()
{
	// Can't dump isHumanPlayer, since it causes spurious desynch dumps when players leave.
	// TODO isHumanPlayer should probably be synchronised, since the game state seems to depend on it, so there might also be a risk of real desynchs when players leave.
	//syncDebug("map = \"%s\", humanPlayers = %d %d %d %d %d %d %d %d", game.map, isHumanPlayer(0), isHumanPlayer(1), isHumanPlayer(2), isHumanPlayer(3), isHumanPlayer(4), isHumanPlayer(5), isHumanPlayer(6), isHumanPlayer(7));
	syncDebug("map = \"%s\"", game.map);

	// Actually send pending droid orders.
	sendQueuedDroidInfo();

	sendPlayerGameTime();
	gameSRand(gameTime);   // Brute force way of synchronising the random number generator, which can't go out of synch.

	if (!paused && !scriptPaused() && !editPaused())
	{
		/* Update the event system */
		if (!bInTutorial)
		{
			eventProcessTriggers(gameTime/SCR_TICKRATE);
		}
		else
		{
			eventProcessTriggers(realTime/SCR_TICKRATE);
		}
		updateScripts();
	}

	// Update abandoned structures
	handleAbandonedStructures();

	// Update the visibility change stuff
	visUpdateLevel();

	// Put all droids/structures/features into the grid.
	gridReset();

	// Check which objects are visible.
	processVisibility();

	// Update the map.
	mapUpdate();

	//update the findpath system
	fpathUpdate();

	// update the cluster system
	clusterUpdate();

	// update the command droids
	cmdDroidUpdate();
	if(getDrivingStatus())
	{
		driveUpdate();
	}

	fireWaitingCallbacks(); //Now is the good time to fire waiting callbacks (since interpreter is off now)

	for (unsigned i = 0; i < MAX_PLAYERS; i++)
	{
		//update the current power available for a player
		updatePlayerPower(i);

		//set the flag for each player
		setHQExists(false, i);
		setSatUplinkExists(false, i);

		numCommandDroids[i] = 0;
		numConstructorDroids[i] = 0;
		numDroids[i]=0;
		numTransporterDroids[i]=0;

		DROID *psNext;
		for (DROID *psCurr = apsDroidLists[i]; psCurr != NULL; psCurr = psNext)
		{
			// Copy the next pointer - not 100% sure if the droid could get destroyed but this covers us anyway
			psNext = psCurr->psNext;
			droidUpdate(psCurr);

			// update the droid counts
			numDroids[i]++;
			switch (psCurr->droidType)
			{
				case DROID_COMMAND:
					numCommandDroids[i] += 1;
					break;
				case DROID_CONSTRUCT:
				case DROID_CYBORG_CONSTRUCT:
					numConstructorDroids[i] += 1;
					break;
				case DROID_TRANSPORTER:
					if( (psCurr->psGroup != NULL) )
					{
						DROID *psDroid = NULL;

						numTransporterDroids[i] += psCurr->psGroup->refCount-1;
						// and count the units inside it...
							for (psDroid = psCurr->psGroup->psList; psDroid != NULL && psDroid != psCurr; psDroid = psDroid->psGrpNext)
							{
							if (psDroid->droidType == DROID_CYBORG_CONSTRUCT || psDroid->droidType == DROID_CONSTRUCT)
								{
									numConstructorDroids[i] += 1;
								}
							if (psDroid->droidType == DROID_COMMAND)
							{
								numCommandDroids[i] += 1;
							}
						}
					}
					break;
				default:
					break;
			}
		}

		numMissionDroids[i]=0;
		for (DROID *psCurr = mission.apsDroidLists[i]; psCurr != NULL; psCurr = psNext)
		{
			/* Copy the next pointer - not 100% sure if the droid could
			get destroyed but this covers us anyway */
			psNext = psCurr->psNext;
			missionDroidUpdate(psCurr);
			numMissionDroids[i]++;
			switch (psCurr->droidType)
			{
				case DROID_COMMAND:
					numCommandDroids[i] += 1;
					break;
				case DROID_CONSTRUCT:
				case DROID_CYBORG_CONSTRUCT:
					numConstructorDroids[i] += 1;
					break;
				case DROID_TRANSPORTER:
					if( (psCurr->psGroup != NULL) )
					{
						numTransporterDroids[i] += psCurr->psGroup->refCount-1;
					}
					break;
				default:
					break;
			}
		}
		for (DROID *psCurr = apsLimboDroids[i]; psCurr != NULL; psCurr = psNext)
		{
			/* Copy the next pointer - not 100% sure if the droid could
			get destroyed but this covers us anyway */
			psNext = psCurr->psNext;

			// count the type of units
			switch (psCurr->droidType)
			{
				case DROID_COMMAND:
					numCommandDroids[i] += 1;
					break;
				case DROID_CONSTRUCT:
				case DROID_CYBORG_CONSTRUCT:
					numConstructorDroids[i] += 1;
					break;
				default:
					break;
			}
		}

		// FIXME: These for-loops are code duplicationo
		/*set this up AFTER droidUpdate so that if trying to building a
		new one, we know whether one exists already*/
		setLasSatExists(false, i);
		STRUCTURE *psNBuilding;
		for (STRUCTURE *psCBuilding = apsStructLists[i]; psCBuilding != NULL; psCBuilding = psNBuilding)
		{
			/* Copy the next pointer - not 100% sure if the structure could get destroyed but this covers us anyway */
			psNBuilding = psCBuilding->psNext;
			structureUpdate(psCBuilding, false);
			//set animation flag
			if (psCBuilding->pStructureType->type == REF_HQ &&
				psCBuilding->status == SS_BUILT)
			{
				setHQExists(true, i);
			}
			if (psCBuilding->pStructureType->type == REF_SAT_UPLINK &&
				psCBuilding->status == SS_BUILT)
			{
				setSatUplinkExists(true, i);
			}
			//don't wait for the Las Sat to be built - can't build another if one is partially built
			if (asWeaponStats[psCBuilding->asWeaps[0].nStat].
				weaponSubClass == WSC_LAS_SAT)
			{
				setLasSatExists(true, i);
			}
		}
		for (STRUCTURE *psCBuilding = mission.apsStructLists[i]; psCBuilding != NULL; psCBuilding = psNBuilding)
		{
			/* Copy the next pointer - not 100% sure if the structure could get destroyed but this covers us anyway. It shouldn't do since its not even on the map!*/
			psNBuilding = psCBuilding->psNext;
			structureUpdate(psCBuilding, true); // update for mission
			if (psCBuilding->pStructureType->type == REF_HQ &&
				psCBuilding->status == SS_BUILT)
			{
				setHQExists(true, i);
			}
			if (psCBuilding->pStructureType->type == REF_SAT_UPLINK &&
				psCBuilding->status == SS_BUILT)
			{
				setSatUplinkExists(true, i);
			}
			//don't wait for the Las Sat to be built - can't build another if one is partially built
			if (asWeaponStats[psCBuilding->asWeaps[0].nStat].
				weaponSubClass == WSC_LAS_SAT)
			{
				setLasSatExists(true, i);
			}
		}
	}

	missionTimerUpdate();

	proj_UpdateAll();

	FEATURE *psNFeat;
	for (FEATURE *psCFeat = apsFeatureLists[0]; psCFeat; psCFeat = psNFeat)
	{
		psNFeat = psCFeat->psNext;
		featureUpdate(psCFeat);
	}

	objmemUpdate();

	// Do completely useless stuff.
	if (!isInSync())
	{
		sendCheck();  // send some pointless checking info if we're doomed anyway
	}


	// Must end update, since we may or may not have ticked, and some message queue processing code may vary depending on whether it's in an update.
	gameTimeUpdateEnd();
}

/* The main game loop */
GAMECODE gameLoop(void)
{
	static uint32_t lastFlushTime = 0;
	static unsigned stateTimes[8];
	static unsigned renderTimes[8];
	static int stateTimeIndex = 0;
	static int renderTimeIndex = 0;

	bool didTick = false;
	while (true)
	{
		// Receive NET_BLAH messages.
		// Receive GAME_BLAH messages, and if it's time, process exactly as many GAME_BLAH messages as required to be able to tick the gameTime.
		recvMessage();

		unsigned renderAverage = std::accumulate(renderTimes, renderTimes + ARRAY_SIZE(renderTimes), 0) / ARRAY_SIZE(renderTimes);
		unsigned stateAverage  = std::accumulate(stateTimes,  stateTimes  + ARRAY_SIZE(stateTimes),  0) / ARRAY_SIZE(stateTimes);

		// Update gameTime and graphicsTime, and corresponding deltas. Note that gameTime and graphicsTime pause, if we aren't getting our GAME_GAME_TIME messages.
		gameTimeUpdate(renderAverage, stateAverage);

		if (deltaGameTime == 0)
		{
			break;  // Not doing a game state update.
		}
		didTick = true;

		ASSERT(!paused && !gameUpdatePaused() && !editPaused(), "Nonsensical pause values.");

		unsigned before = wzGetTicks();
		syncDebug("Begin game state update, gameTime = %d", gameTime);
		gameStateUpdate();
		syncDebug("End game state update, gameTime = %d", gameTime);
		unsigned after = wzGetTicks();

		stateTimes[stateTimeIndex] = after - before;
		stateTimeIndex = (stateTimeIndex + 1)%ARRAY_SIZE(stateTimes);

		ASSERT(deltaGraphicsTime == 0, "Shouldn't update graphics and game state at once.");
	}

	if (didTick || realTime - lastFlushTime < 400u)
	{
		lastFlushTime = realTime;
		NETflush();  // Make sure the game time tick message is really sent over the network, and that we aren't waiting too long to send data.
	}

	unsigned before = wzGetTicks();
	GAMECODE renderReturn = renderLoop();
	unsigned after = wzGetTicks();

	renderTimes[renderTimeIndex] = after - before;
	renderTimeIndex = (renderTimeIndex + 1)%ARRAY_SIZE(renderTimes);

	return renderReturn;
}

/* The video playback loop */
void videoLoop(void)
{
	bool videoFinished;

	ASSERT( videoMode == 1, "videoMode out of sync" );

	// display a frame of the FMV
	videoFinished = !seq_UpdateFullScreenVideo(NULL);
	pie_ScreenFlip(CLEAR_BLACK);

	// should we stop playing?
	if (videoFinished || keyPressed(KEY_ESC) || mouseReleased(MOUSE_LMB))
	{
		seq_StopFullScreenVideo();

		//set the next video off - if any
		if (videoFinished && seq_AnySeqLeft())
		{
			seq_StartNextFullScreenVideo();
		}
		else
		{
			// remove the intelligence screen if necessary
			if (messageIsImmediate())
			{
				intResetScreen(true);
				setMessageImmediate(false);
			}
			//don't do the callback if we're playing the win/lose video
			if (!getScriptWinLoseVideo())
			{
				eventFireCallbackTrigger((TRIGGER_TYPE)CALL_VIDEO_QUIT);
			}
			else
			{
				displayGameOver(getScriptWinLoseVideo() == PLAY_WIN);
			}
		}
	}
}


void loop_SetVideoPlaybackMode(void)
{
	videoMode += 1;
	paused = true;
	video = true;
	gameTimeStop();
	pie_SetFogStatus(false);
	audio_StopAll();
	wzShowMouse(false);
	screen_StopBackDrop();
	pie_ScreenFlip(CLEAR_BLACK);
}


void loop_ClearVideoPlaybackMode(void)
{
	videoMode -=1;
	paused = false;
	video = false;
	gameTimeStart();
	pie_SetFogStatus(true);
	cdAudio_Resume();
	wzShowMouse(true);
	ASSERT( videoMode == 0, "loop_ClearVideoPlaybackMode: out of sync." );
}


SDWORD loop_GetVideoMode(void)
{
	return videoMode;
}

bool loop_GetVideoStatus(void)
{
	return video;
}

bool editPaused(void)
{
	return pauseState.editPause;
}

void setEditPause(bool state)
{
	pauseState.editPause = state;
}

bool gamePaused( void )
{
	return paused;
}

void setGamePauseStatus( bool val )
{
	paused = val;
}

bool gameUpdatePaused(void)
{
	return pauseState.gameUpdatePause;
}
bool audioPaused(void)
{
	return pauseState.audioPause;
}
bool scriptPaused(void)
{
	return pauseState.scriptPause;
}
bool scrollPaused(void)
{
	return pauseState.scrollPause;
}
bool consolePaused(void)
{
	return pauseState.consolePause;
}

void setGameUpdatePause(bool state)
{
	pauseState.gameUpdatePause = state;
}
void setAudioPause(bool state)
{
	pauseState.audioPause = state;
}
void setScriptPause(bool state)
{
	pauseState.scriptPause = state;
}
void setScrollPause(bool state)
{
	pauseState.scrollPause = state;
}
void setConsolePause(bool state)
{
	pauseState.consolePause = state;
}

//set all the pause states to the state value
void setAllPauseStates(bool state)
{
	setGameUpdatePause(state);
	setAudioPause(state);
	setScriptPause(state);
	setScrollPause(state);
	setConsolePause(state);
}

UDWORD	getNumDroids(UDWORD player)
{
	return(numDroids[player]);
}

UDWORD	getNumTransporterDroids(UDWORD player)
{
	return(numTransporterDroids[player]);
}

UDWORD	getNumMissionDroids(UDWORD player)
{
	return(numMissionDroids[player]);
}

UDWORD	getNumCommandDroids(UDWORD player)
{
	return numCommandDroids[player];
}

UDWORD	getNumConstructorDroids(UDWORD player)
{
	return numConstructorDroids[player];
}


// increase the droid counts - used by update factory to keep the counts in sync
void incNumDroids(UDWORD player)
{
	numDroids[player] += 1;
}
void incNumCommandDroids(UDWORD player)
{
	numCommandDroids[player] += 1;
}
void incNumConstructorDroids(UDWORD player)
{
	numConstructorDroids[player] += 1;
}

/* Fire waiting beacon messages which we couldn't run before */
static void fireWaitingCallbacks(void)
{
	bool bOK = true;

	while(!isMsgStackEmpty() && bOK)
	{
		bOK = msgStackFireTop();
		if(!bOK){
			ASSERT(false, "fireWaitingCallbacks: msgStackFireTop() failed (stack count: %d)", msgStackGetCount());
		}
	}
}
