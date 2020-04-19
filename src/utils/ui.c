//
//  ui.c
//  C-ray
//
//  Created by Valtteri Koskivuori on 19/02/2017.
//  Copyright © 2015-2020 Valtteri Koskivuori. All rights reserved.
//

#include "../includes.h"
#include "ui.h"

#include "../renderer/renderer.h"
#include "logging.h"
#include "../datatypes/tile.h"
#include "../datatypes/texture.h"
#include "../datatypes/color.h"
#include "../utils/platform/thread.h"
#include "../utils/platform/signal.h"

static bool aborted = false;

//FIXME: This won't work on linux, it'll just abort the execution.
//Take a look at the docs for sigaction() and implement that.
void sigHandler(int sig) {
	if (sig == 2) { //SIGINT
		printf("\n");
		logr(info, "Received ^C, aborting render without saving\n");
		aborted = true;
	}
}

int initSDL(struct display *d) {
#ifdef UI_ENABLED
	if (!d->enabled) {
		return 0;
	}
	
	//Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		logr(warning, "SDL couldn't initialize, error: \"%s\"\n", SDL_GetError());
		return -1;
	}
	//Init window
	SDL_WindowFlags flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
	if (d->isFullScreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	if (d->isBorderless) flags |= SDL_WINDOW_BORDERLESS;
	flags |= SDL_WINDOW_RESIZABLE;
	
	d->window = SDL_CreateWindow("C-ray © VKoskiv 2015-2020",
								 SDL_WINDOWPOS_UNDEFINED,
								 SDL_WINDOWPOS_UNDEFINED,
								 d->width * d->windowScale,
								 d->height * d->windowScale,
								 flags);
	if (d->window == NULL) {
		logr(warning, "Window couldn't be created, error: \"%s\"\n", SDL_GetError());
		return -1;
	}
	//Init renderer
	d->renderer = SDL_CreateRenderer(d->window, -1, SDL_RENDERER_ACCELERATED);
	if (d->renderer == NULL) {
		logr(warning, "Renderer couldn't be created, error: \"%s\"\n", SDL_GetError());
		return -1;
	}
	
	SDL_RenderSetLogicalSize(d->renderer, d->width, d->height);
	//And set blend modes
	SDL_SetRenderDrawBlendMode(d->renderer, SDL_BLENDMODE_BLEND);
	
	SDL_RenderSetScale(d->renderer, d->windowScale, d->windowScale);
	//Init pixel texture
	d->texture = SDL_CreateTexture(d->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, d->width, d->height);
	if (d->texture == NULL) {
		logr(warning, "Texture couldn't be created, error: \"%s\"\n", SDL_GetError());
		return -1;
	}
	//Init overlay texture (for UI info)
	d->overlayTexture = SDL_CreateTexture(d->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, d->width, d->height);
	if (d->overlayTexture == NULL) {
		logr(warning, "Overlay texture couldn't be created, error: \"%s\"\n", SDL_GetError());
		return -1;
	}
	
	//And set blend modes for textures too
	SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(d->overlayTexture, SDL_BLENDMODE_BLEND);
#endif
	return 0;
}

void destroyDisplay(struct display *d) {
#ifdef UI_ENABLED
	if (d) {
		if (d->window) {
			SDL_DestroyWindow(d->window);
		}
		if (d->renderer) {
			SDL_DestroyRenderer(d->renderer);
		}
		if (d->texture) {
			SDL_DestroyTexture(d->texture);
		}
		if (d->overlayTexture) {
			SDL_DestroyTexture(d->overlayTexture);
		}
		free(d);
	}
#endif
}

void printDuration(uint64_t ms) {
	logr(info, "Finished render in ");
	printSmartTime(ms);
	printf("                     \n");
}

void getKeyboardInput(struct renderer *r) {
	if (aborted) {
		r->state.renderAborted = true;
	}
	static bool sigRegistered = false;
	//Check for CTRL-C
	if (!sigRegistered) {
		if (registerHandler(sigint, sigHandler)) {
			logr(warning, "Unable to catch SIGINT\n");
		}
		sigRegistered = true;
	}
#ifdef UI_ENABLED
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
			if (event.key.keysym.sym == SDLK_s) {
				printf("\n");
				logr(info, "Aborting render, saving\n");
				r->state.renderAborted = true;
				r->state.saveImage = true;
			}
			if (event.key.keysym.sym == SDLK_x) {
				printf("\n");
				logr(info, "Aborting render without saving\n");
				r->state.renderAborted = true;
				r->state.saveImage = false;
			}
			if (event.key.keysym.sym == SDLK_p) {
				for (int i = 0; i < r->prefs.threadCount; ++i) {
					if (r->state.threads[i].paused) {
						r->state.threads[i].paused = false;
					} else {
						r->state.threads[i].paused = true;
					}
				}
			}
		}
	}
#endif
}

void clearProgBar(struct renderer *r, struct renderTile temp) {
	for (unsigned i = 0; i < temp.width; ++i) {
		blit(r->state.uiBuffer, clearColor, temp.begin.x + i, (temp.begin.y + (temp.height/5)) - 1);
		blit(r->state.uiBuffer, clearColor, temp.begin.x + i, (temp.begin.y + (temp.height/5))    );
		blit(r->state.uiBuffer, clearColor, temp.begin.x + i, (temp.begin.y + (temp.height/5)) + 1);
	}
}

/*
 So this is a bit of a kludge, we get the dynamically updated completedSamples
 info that renderThreads report back, and then associate that with the static
 renderTiles array data that is only updated once a tile is completed.
 I didn't want to put any mutex locks in the main render loop, so this gets
 around that.
 */
void drawProgressBars(struct renderer *r) {
	for (int t = 0; t < r->prefs.threadCount; ++t) {
		if (r->state.threads[t].currentTileNum != -1) {
			struct renderTile temp = r->state.renderTiles[r->state.threads[t].currentTileNum];
			int completedSamples = r->state.threads[t].completedSamples;
			int totalSamples = r->prefs.sampleCount;
			
			float prc = ((float)completedSamples / (float)totalSamples);
			int pixels2draw = (int)((float)temp.width*(float)prc);
			
			//And then draw the bar
			for (int i = 0; i < pixels2draw; ++i) {
				blit(r->state.uiBuffer, progColor, temp.begin.x + i, (temp.begin.y + (temp.height/5)) - 1);
				blit(r->state.uiBuffer, progColor, temp.begin.x + i, (temp.begin.y + (temp.height/5))    );
				blit(r->state.uiBuffer, progColor, temp.begin.x + i, (temp.begin.y + (temp.height/5)) + 1);
			}
		}
	}
}

/**
 Draw highlight frame to show which tiles are rendering

 @param r Renderer
 @param tile Given renderTile
 */
void drawFrame(struct renderer *r, struct renderTile tile) {
	int length = tile.width  <= 16 ? 4 : 8;
		length = tile.height <= 16 ? 4 : 8;
	struct color c = clearColor;
	if (tile.isRendering) {
		c = frameColor;
	} else if (tile.renderComplete) {
		c = clearColor;
	} else {
		return;
	}
	for (int i = 1; i < length; ++i) {
		//top left
		blit(r->state.uiBuffer, c, tile.begin.x+i, tile.begin.y+1);
		blit(r->state.uiBuffer, c, tile.begin.x+1, tile.begin.y+i);
		
		//top right
		blit(r->state.uiBuffer, c, tile.end.x-i, tile.begin.y+1);
		blit(r->state.uiBuffer, c, tile.end.x-1, tile.begin.y+i);
		
		//Bottom left
		blit(r->state.uiBuffer, c, tile.begin.x+i, tile.end.y-1);
		blit(r->state.uiBuffer, c, tile.begin.x+1, tile.end.y-i);
		
		//bottom right
		blit(r->state.uiBuffer, c, tile.end.x-i, tile.end.y-1);
		blit(r->state.uiBuffer, c, tile.end.x-1, tile.end.y-i);
	}
}

void updateFrames(struct renderer *r) {
	if (r->prefs.tileWidth < 8 || r->prefs.tileHeight < 8) return;
	for (int i = 0; i < r->state.tileCount; ++i) {
		//For every tile, if it's currently rendering, draw the frame
		//If it is NOT rendering, clear any frame present
		drawFrame(r, r->state.renderTiles[i]);
		if (r->state.renderTiles[i].renderComplete) {
			clearProgBar(r, r->state.renderTiles[i]);
		}
	}
	drawProgressBars(r);
}

void drawWindow(struct renderer *r, struct texture *t) {
	if (aborted) {
		r->state.renderAborted = true;
	}
#ifdef UI_ENABLED
	//Render frames
	updateFrames(r);
	//Update image data
	SDL_UpdateTexture(r->mainDisplay->texture, NULL, t->byte_data, t->width * 3);
	SDL_UpdateTexture(r->mainDisplay->overlayTexture, NULL, r->state.uiBuffer->byte_data, t->width * 4);
	SDL_RenderCopy(r->mainDisplay->renderer, r->mainDisplay->texture, NULL, NULL);
	SDL_RenderCopy(r->mainDisplay->renderer, r->mainDisplay->overlayTexture, NULL, NULL);
	SDL_RenderPresent(r->mainDisplay->renderer);
#endif
}
