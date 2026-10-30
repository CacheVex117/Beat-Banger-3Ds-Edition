"""Hard limits of the native Beat Banger 3DS runtime (native3ds @ 137a0d7) that a converted level must respect.

A value over a limit does not stop conversion: the binaries can carry it, but the current runtime would
reject or truncate it, so the level is reported as not runtime-compatible. Raising a limit is a runtime
change, after which only this table needs updating.
"""

# core/rhythm_core.h:52-55 (BBCH v1 loader rejects charts over these; notes/loops raised for SD mods)
MAX_NOTES = 1024
MAX_SHUTTERS = 64
MAX_LOOPS = 256
MAX_BPM_CHANGES = 16

# platform/3ds/anim.h:19-22 (BBAN v3)
MAX_SHEETS = 64
MAX_PAGES_PER_SHEET = 4
MAX_FRAMES_PER_SHEET = 16
MAX_CAMERA_KEYS = 256                # raised from 64 (native3ds anim.h, Lola Bunny - Horny Bunny Season has 78)
MAX_VARIATIONS_PER_SHEET = 2
ANIM_FILE_MAX = 64 * 1024            # anim.c file size check
ANIM_RESIDENT_BUDGET = 12 * 1024 * 1024   # hw_profile.c anim_resident_bytes (above it sheets stream); same on Old and New 3DS

# platform/3ds/sfx.h:17-20 (BBSF v1)
MAX_CLIPS = 512                      # BB_SFX_MAX_CLIPS (raised from 128: Beat Banger Encore has 252)
MAX_SOUND_LOOPS = 64
MAX_ONESHOTS = 512
MAX_VOICE_BANKS = 64
MAX_LOOP_CLIPS = 32                  # BB_SFX_LOOP_CLIPS: clips per sound_loop (BBSF v2; v1 records hold 8)
V1_LOOP_SLOTS = 8
MAX_VOICE_CLIPS = 48                 # BB_SFX_VOICE_CLIPS: clips per voice_bank (BBSF v2; v1 records hold 12)
V1_VOICE_SLOTS = 12
SFX_RATES = (32000, 22050, 16000)    # a bank over SFX_FILE_MAX is rebuilt at the next lower rate (sfx.c plays any rate)
SFX_FILE_MAX = 8 * 1024 * 1024       # whole bank is copied to linear memory

# platform/3ds/video.h (BBVD v2)
MAX_VIDEOS = 16                      # <id>_v0..v15 probed (main.c, BB_VIDEO_MAX)
MAX_VIDEO_PAGES = 384                # 8 frames per 1024x1024 ETC1 page -> 3072 frames (pages + soundtrack stream)

# platform/3ds/main.c (stage index, native3ds cf39d5d+), save_data.h (scores)
BUILTIN_STAGES = 8
MAX_STAGES = 64 - BUILTIN_STAGES     # MAX_STAGES 64 is shared with the built-in stages: mod levels the game can list
MAX_SCORED_STAGES = 64 - BUILTIN_STAGES   # BB_SAVE_MAX_STAGES 64 (native3ds 3AA): every listed mod level keeps its scores
MAX_PROGRESS_PACKS = 32 - 3               # BB_SAVE_MAX_ACTS 32, a slot kept for each of the 3 built-in volumes
MAX_ACTS = 8                         # built-in volumes only; mod packs do not take act slots
STAGE_ID_MAX = 23                    # BB_SAVE_ID_LEN 24: longer ids would share save slots
TITLE_MAX = 47
CHARACTER_MAX = 23
ACT_ID_MAX = 23
ACT_NAME_MAX = 31
NAME_FIELD_MAX = 31                  # every char[32] name field in the binaries

# path buffers: anim.h gfx_dir[64] is the tightest ("<root>/stages/<id>/gfx" must stay < 64 bytes)
GFX_DIR_MAX = 63

DIFFICULTY_SLOTS = ("virgin", "normal", "expert")   # main.c:106 k_diff_name
SAMPLE_RATE = 32000                                  # audio_ndsp / sfx / video PCM rate
