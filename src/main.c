#include <genesis.h>
#include <maths.h>
#include <memory.h>
#include "resources.h"

#define BULLETS_PER_PLAYER 2
#define MAX_PARTICLES 24

#define N_PLAYERS 2
#define START_HEALTH 3

#define Y_HORIZON 160
#define X_MIN 10
#define X_MAX 280

#define COLLISION_THRESH 128

typedef struct {
	Sprite *sprite;
    u16 pal;
	s16 x, y;
    s16 dx, dy;
    s16 ddx, ddy;
    s8 facing;
    s16 ttl;
    bool ai_solved;
} Entity;

bool Entity_collide(Entity *e1, Entity *e2);

Entity *Entity_new(
    const SpriteDefinition *spriteDef,
    u8 pal,
    s16 x,
    s16 y,
    s16 ttl
    ) {
    Entity *ret = malloc(sizeof(Entity));
    ret->sprite = SPR_addSprite(
        spriteDef,
        x,
        y,
        TILE_ATTR(pal, TRUE, FALSE, FALSE)
        );
    ret->x = x;
    ret->y = y;
    ret->dx = 0;
    ret->dy = 0;
    ret->ddx = 0;
    ret->ddy = 0;
    ret->facing = 1;
    ret->ttl = ttl;
    ret->ai_solved = FALSE;
    return ret;
}

Entity *Entity_del(Entity *e) {
    if (!e) {
        return NULL;
    }
    SPR_releaseSprite(e->sprite);
    free(e);
    return NULL;
}

Entity *Entity_update(Entity *e) {
    if (!e) {
        return NULL;
    }
    if (e->ttl > 0) {
        e->ttl -= 1;
        if (e->ttl == 0) {
            return Entity_del(e);
        }
    }
    e->dx += e->ddx;
    e->dy += e->ddy;
    e->x += e->dx;
    e->y += e->dy;
    SPR_setPosition(e->sprite, e->x, e->y);
    return e;
}

typedef enum {
    AI_ATTACK,
    AI_DEFEND
} AI_State;

typedef struct {
    u8 player_no;
    u8 ctrl_no;
    Entity *e;
    Entity *bullets[BULLETS_PER_PLAYER];
    u8 cooldown_b;
    u8 cooldown_c;
    u8 jumps;
    u8 anim_frames;
    u8 pal;
    u8 health;
    Entity *health_notches[START_HEALTH];
    AI_State ai_state;
    u16 ai_state_frames;
} Player;

Player *Player_new(
    u8 player_no,
    u8 ctrl_no,
    const SpriteDefinition *spriteDef,
    u8 pal,
    s16 x,
    s16 y
    ) {
    Player *ret = malloc(sizeof(Player));
    ret->player_no = player_no;
    ret->ctrl_no = ctrl_no;
    ret->e = Entity_new(spriteDef, pal, x, y, 0);
    for (int i = 0; i < BULLETS_PER_PLAYER; ++i) {
        (ret->bullets)[i] = NULL;
    }
    ret->cooldown_b = 0;
    ret->cooldown_c = 0;
    ret->jumps = 0;
    ret->anim_frames = 0;
    ret->pal = pal;
    ret->health = START_HEALTH;
    ret->ai_state = AI_ATTACK;
    ret->ai_state_frames = 0;
    s16 hx = player_no * 150;
    for (int i = 0; i < START_HEALTH; ++i) {
        (ret->health_notches)[i] = Entity_new(
            &SPR_Health,
            pal == PAL1 ? PAL3 : pal,
            hx,
            20,
            0
            );
        hx += 16;
    }
    return ret;
}

Player *Player_del(Player *p) {
    for (int i = 0; i < BULLETS_PER_PLAYER; ++i) {
        Entity_del((p->bullets)[i]);
    }
    for (int i = 0; i < START_HEALTH; ++i) {
        Entity_del((p->health_notches)[i]);
    }
    Entity_del(p->e);
    free(p);
    return NULL;
}

typedef struct {
    Player *players[N_PLAYERS];
    Entity *particles[MAX_PARTICLES];
    u8 paused;
    u8 state;
    /* states:
     * 0 playing
     * 1 waiting to start
     * 2 waiting for death
     */
    u8 timer;
} Game;

Game *Game_new(u8 p2_ctl_no) {
    Game *g = malloc(sizeof(Game));
    Player **players = g->players;
    players[0] = Player_new(0, 0, &SPR_Cleric, PAL1, 10, Y_HORIZON);
    players[1] = Player_new(1, p2_ctl_no, &SPR_Cleric, PAL2, X_MAX, Y_HORIZON);
    players[1]->e->facing = -1;
    SPR_setHFlip(players[1]->e->sprite, TRUE);
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        (g->particles)[i] = NULL;
    }
    g->paused = 1;
    g->state = 1;
    g->timer = 80;
    return g;
}

Game *Game_del(Game *g) {
    Player **players = g->players;
    Player_del(players[0]);
    Player_del(players[1]);
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Entity_del((g->particles)[i]);
    }
    return g;
}

void add_particles(Entity **particles, s16 x, s16 y, u8 pal, u8 pattern) {
    int i = 0;
    for (int pi = 0; pi < 4; ++pi) {
        for (; i < MAX_PARTICLES; ++i) {
            if (!(particles[i])) {
                break;
            }
        }
        if (i >= MAX_PARTICLES) {
            // No remaining slots. give up
            return;
        }
        Entity *e = Entity_new(
            &SPR_Particles,
            pal,
            x,
            y,
            10
        );
        s16 dx;
        s16 dy;
        if (pattern == 0) {
            // circle-ish
            dy = (random() & 3) - 2;
            dx = (random() & 3) - 2;
        } else { // jump-ish
            dy = 1;
            dx = (2 - pi) << 1;
        }
        e->dx = dx;
        e->dy = dy;
        particles[i] = e;
    }
}

void fireball(Player *p, s16 dy) {
    if (p->cooldown_b > 0) {
        return;
    }
    Entity **bullets = p->bullets;
    int i;
    for (i = 0; i < BULLETS_PER_PLAYER; ++i) {
        if (!(bullets[i])) {
            break;
        }
    }
    if (i >= BULLETS_PER_PLAYER) {
        // No remaining slots. give up
        return;
    }
    u8 pal = p->pal == PAL1 ? PAL3 : p->pal;
    Entity *e = Entity_new(
        &SPR_Fireball,
        pal,
        p->e->x,
        p->e->y,
        30);
    e->dx = 10 * p->e->facing;
    e->dy = dy;
    bullets[i] = e;
    p->cooldown_b = 30;
    p->anim_frames=10;
    SPR_setAnimAndFrame(p->e->sprite, 3, 3);
    XGM_startPlayPCM(64,1,SOUND_PCM_CH2);
}

void jump(Player *p, Entity **particles) {
    if (p->cooldown_c > 0) {
        return;
    }
    if (p->jumps > 1) {
        return;
    }
    p->e->dy = 0;
    p->e->ddy = -5;
    p->cooldown_c = 10;
    p->jumps += 1;
    add_particles(particles, p->e->x, p->e->y + 8, p->pal, 1);
    XGM_startPlayPCM(65,1,SOUND_PCM_CH3);
}

bool will_collide(Entity *e1, Entity *e2) {
    Entity g1, g2;
    g1.x = e1->x;
    g1.y = e1->y;
    g2.x = e2->x;
    g2.y = e2->y;
    for (int ts = 0; ts < 32; ++ts) {
        //g1.x += e1->dx;
        //g1.y += e1->dy;
        g2.x += e2->dx;
        g2.y += e2->dy;
        if (Entity_collide(&g1, &g2)) return TRUE;
    }
    return FALSE;
}

bool fireball_available(Player *p) {
    if (p->cooldown_b > 0) return FALSE;
    for (int i = 0; i < BULLETS_PER_PLAYER; ++i) {
        Entity *b = p->bullets[i];
        if (!b) return TRUE;
    }
    return FALSE;
}

void ai(Player *p, Player *op, Entity **particles) {
    p->e->dx = 0;
    bool jumped = FALSE;
    for (int i = 0; i < BULLETS_PER_PLAYER; ++i) {
        Entity *b = op->bullets[i];
        if (!b) continue;
        if (b->ai_solved) continue;
        if (!will_collide(p->e, b)) {
            b->ai_solved = TRUE;
            continue;
        }
        if (fireball_available(p)) {
            s16 dy;
            if (b->y < p->e->y) {
                dy = -6;
            } else if (b->y == p->e->y) {
                dy = 0;
            } else {
                dy = 6;
            }
            Entity ghost_fb;
            ghost_fb.x = p->e->x;
            ghost_fb.y = p->e->y;
            ghost_fb.dx = 10 * p->e->facing;
            ghost_fb.dy = dy;
            if (will_collide(&ghost_fb, b)) {
                fireball(p, dy);
                b->ai_solved = TRUE;
                continue;
            }
        }
        if (b->dy == 0 && (!jumped) && abs(p->e->x - b->x) <= 128) {
            jump(p, particles);
            jumped = TRUE;
            b->ai_solved = TRUE;
        }
        if (p->ai_state == AI_ATTACK) {
            if (p->e->x >= op->e->x && p->e->x > X_MIN) {
                p->e->dx = -4;
            } else if (p->e->x <= op->e->x && p->e->x < X_MAX) {
                p->e->dx = 4;
            }
        } else if (p->ai_state == AI_DEFEND) {
            if (p->e->x >= op->e->x && op->e->x < X_MAX) {
                p->e->dx = 4;
            } else if (p->e->x <= op->e->x && p->e->x > X_MIN) {
                p->e->dx = -4;
            }
        }
    }
    if (fireball_available(p)) {
        if (
            (p->ai_state == AI_ATTACK && random() < 10000) ||
            (p->ai_state == AI_DEFEND && random() < 5000)
            ) {
            s16 dy;
            if (op->e->y < p->e->y) {
                dy = -6;
            } else if (op->e->y == p->e->y) {
                dy = 0;
            } else {
                dy = 6;
            }
            fireball(p, dy);
        } 
    }
    if (p->e->dx == 0) {
        if (p->ai_state == AI_ATTACK) {
            if (p->e->x >= op->e->x + 4 && p->e->x > X_MIN) {
                p->e->dx = -4;
            } else if (p->e->x <= op->e->x - 4 && p->e->x < X_MAX) {
                p->e->dx = 4;
            }
        } else if (p->ai_state == AI_DEFEND) {
            if (p->e->x >= op->e->x && p->e->x < X_MAX) {
                p->e->dx = 4;
            } else if (p->e->x <= op->e->x && p->e->x > X_MIN) {
                p->e->dx = -4;
            }
        }
    }
    
    if (random() < 4048 && (!jumped)) {
        jump(p, particles);
    }
    p->ai_state_frames += 1;
    if (p->ai_state_frames > random() >> 7) {
        p->ai_state_frames = 0;
        p->ai_state = !p->ai_state;
    }
}

void input(Game *g) {
    u16 joy;
    joy = JOY_readJoypad(JOY_2);
    if (joy & BUTTON_START) {
        // player 2 is playing
        XGM_startPlayPCM(68,1,SOUND_PCM_CH3);
        (g->players)[1]->ctrl_no = 1;
    }
    if (!(g->paused)) {
        Player **players = g->players;
        for(int i = 0; i < N_PLAYERS; ++i) {
            Player *p = players[i];
            if (p->ctrl_no > 1) {
                // CPU player
                ai(p, players[0], g->particles);
                return;
            }
            if (p->ctrl_no == 0) {
                joy = JOY_readJoypad(JOY_1);
            } else if (p->ctrl_no == 1) {
                joy = JOY_readJoypad(JOY_2);
            }
            if (p->e->y >= Y_HORIZON) {
                p->e->dx = 0;
            }
            if (p->e->dy >= 0) {
                if(joy & BUTTON_LEFT) {
                    p->e->dx = -4;
                } else if(joy & BUTTON_RIGHT) {
                    p->e->dx = 4;
                }
            }
            s16 fb_dy = 0;
            if(joy & BUTTON_UP) {
                fb_dy = -9;
            } else if(joy & BUTTON_DOWN) {
                fb_dy = 10;
            }
            if (joy & BUTTON_B) {
                fireball(p, fb_dy);
            }
            if (joy & BUTTON_C) {
                jump(p, g->particles);
            }
        }
    }
}

bool Entity_collide(Entity *e1, Entity *e2) {
    s16 dx = e1->x - e2->x;
    s16 dy = e1->y - e2->y;
    return dx*dx + dy*dy <= COLLISION_THRESH;
}

void collisions(Game *g) {
    Player **players = g->players;
    for (int pi = 0; pi < N_PLAYERS; ++pi) {
        Player *p = players[pi];
        Entity **bullets = p->bullets;
        for (int bi = 0; bi < BULLETS_PER_PLAYER; ++bi) {
            Entity *b = bullets[bi];
            if (!b) {
                continue;
            }
            for (int opi = 0; opi < N_PLAYERS; ++opi) {
                if (pi == opi) {
                    continue;
                }
                Player *op = players[opi];
                if (Entity_collide(op->e, b)) {
                    // hit other player
                    XGM_startPlayPCM(66,1,SOUND_PCM_CH3);
                    b->ttl = 1;
                    add_particles(g->particles, b->x, b->y, PAL3, 0);
                    op->health -= 1;
                    (op->health_notches)[op->health] = Entity_del((op->health_notches)[op->health]);
                    if (op->health == 0) {
                        XGM_startPlayPCM(67,1,SOUND_PCM_CH2);
                        op->e->dx = 0;
                        g->paused = 1;
                        g->state = 2;
                        g->timer = 20;
                        SPR_setAnimAndFrame(op->e->sprite, 4, 0);
                        op->anim_frames = 20;
                    }

                    continue;
                } 
                for (int obi = 0; obi < BULLETS_PER_PLAYER; ++obi) {
                    Entity *ob = op->bullets[obi];
                    if (Entity_collide(ob, b)) {
                        b->ttl = 1;
                        ob->ttl = 1;
                    }
                }

            }
        }
    }
}

void sprites(Game *g) {
    Player **players = g->players;
    for (int pi = 0; pi < N_PLAYERS; ++pi) {
        Player *p = players[pi];

        if (p->cooldown_b > 0) {
            p->cooldown_b -= 1;
        }
        if (p->cooldown_c > 0) {
            p->cooldown_c -= 1;
        }
        if (p->anim_frames > 0) {
            p->anim_frames -= 1;
            if(p->anim_frames == 0) {
                SPR_setAnim(p->e->sprite, 0);
            }
        }

        if (p->e->ddy < 4) {
            p->e->ddy += 1;
        }
        if (p->e->y > Y_HORIZON) {
            p->e->y = Y_HORIZON;
            p->e->dy = 0;
            p->jumps = 0;
        }
        if (p->e->x >= X_MAX) {
            p->e->x = X_MAX;
        }
        if (p->e->x <= X_MIN) {
            p->e->x = X_MIN;
        }

        Entity_update(p->e);
        Entity **bullets = p->bullets;
        for (int bi = 0; bi < BULLETS_PER_PLAYER; ++bi) {
            bullets[bi] = Entity_update(bullets[bi]);
        }
        Entity **health_notches = p->health_notches;
        for (int hi = 0; hi < START_HEALTH; ++hi) {
            health_notches[hi] = Entity_update(health_notches[hi]);
        }
    }
    Player *p = players[0];
    Player *op = players[1];
    if (p->e->x >= op->e->x) {
        p->e->facing = -1;
        SPR_setHFlip(p->e->sprite, TRUE);
        op->e->facing = 1;
        SPR_setHFlip(op->e->sprite, FALSE);
    } else {
        p->e->facing = 1;
        SPR_setHFlip(p->e->sprite, FALSE);
        op->e->facing = -1;
        SPR_setHFlip(op->e->sprite, TRUE);
    }
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        (g->particles)[i] = Entity_update((g->particles)[i]);
    }
    SPR_update();
}

int main() {

    JOY_init();
    SPR_init();
	SYS_disableInts();

    PAL_setPalette(PAL0, IMG_BG.palette->data, DMA);
    PAL_setPalette(PAL1, SPR_Cleric.palette->data, DMA);
    PAL_setPalette(PAL2, PAL_CrystalDecay.data, DMA);
    PAL_setPalette(PAL3, SPR_Fireball.palette->data, DMA);

    XGM_setPCM(64, WAV_FireballFire, sizeof(WAV_FireballFire));
    XGM_setPCM(65, WAV_Boing, sizeof(WAV_Boing));
    XGM_setPCM(66, WAV_Oof, sizeof(WAV_Oof));
    XGM_setPCM(67, WAV_Ouch, sizeof(WAV_Ouch));
    XGM_setPCM(68, WAV_Ding, sizeof(WAV_Ding));

	SYS_enableInts();

    // production screen
    VDP_drawImage(BG_A, &IMG_Production, 0, 0);
    for (int i = 0; i < 90; ++i) {
        VDP_waitVSync();
    }

    // title screen
    u8 p2_ctrl_no;
    VDP_drawImage(BG_A, &IMG_Title, 0, 0);
    while (TRUE) {
        random();
        SYS_doVBlankProcess();
        if (JOY_readJoypad(JOY_1) & BUTTON_START) {
            p2_ctrl_no = 2;
            break;
        }
        if (JOY_readJoypad(JOY_2) & BUTTON_START) {
            p2_ctrl_no = 1;
            break;
        }
        VDP_waitVSync();
    }

    VDP_drawImage(VDP_BG_A, &IMG_BG, 0, 0);

    Game *g = Game_new(p2_ctrl_no); // 2 is AI, 1 is player

	while(TRUE) {
        if (g->state == 1) {
            g->timer -= 1;
            if (g->timer == 60) {
                XGM_startPlayPCM(64,1,SOUND_PCM_CH2);
            } else if (g->timer == 40) {
                XGM_startPlayPCM(64,1,SOUND_PCM_CH2);
            } else if (g->timer == 20) {
                XGM_startPlayPCM(64,1,SOUND_PCM_CH2);
            } else if (g->timer == 0) {
                XGM_startPlayPCM(68,1,SOUND_PCM_CH3);
                g->paused = 0;
                g->state = 0;
                g->particles[0] = Entity_new(
                    &SPR_GO,
                    PAL3,
                    80,
                    20,
                    20
                    );
            }
        } else if (g->state == 2) {
            g->timer -= 1;
            if (g->timer == 0) {
                p2_ctrl_no = (g->players)[1]->ctrl_no;
                Game_del(g);
                g = Game_new(p2_ctrl_no);
            }
        }

        input(g);
        if (g->state == 0) collisions(g);
        sprites(g);
        SYS_doVBlankProcess();
		VDP_waitVSync();
	}
	return 0;
}

/* TODO
 * blastem/real hardware
 * random crash bug
 */
