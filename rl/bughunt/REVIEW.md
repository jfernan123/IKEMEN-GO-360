# Bug hunt: reviewing recorded fights

You are reviewing frames from **mugen360**, a C++ port of the Ikemen-GO fighting game engine (a MUGEN clone), running
the MUGEN game "DC vs MARVEL 2025" (Marvel vs Capcom style sprites and lifebars). The clips come straight from the
engine. Player 1 (left at the start) presses random buttons; player 2 is the computer (SmartAI) playing the character
named in the clip's name. You are looking for **engine bugs**: things the real game (MUGEN / Ikemen) would draw
differently. You can't see the reference game, so judge by what a finished 2D fighting game should look like, and by
consistency within the clip.

## What to look for

- **Misplaced effects:** sparks, explosions, projectiles, super backgrounds or flashes in the wrong place (far from
  who made them, off to one side, cut off, covering only part of the screen when it should cover all of it).
- **Leftovers:** a sprite, effect, ghost/afterimage or helper that stays on screen, frozen, after its move ended.
- **Wrong layering:** a super background drawn over the lifebars, characters hidden behind the stage, effects behind
  something they should be in front of.
- **Broken sprites:** wrong colours (pure black or pink/magenta blocks, a palette that doesn't match the rest of the
  character), garbage pixels, a sprite cut in half, a frame of a different character.
- **Impossible positions:** a character floating high above the floor with no reason, stuck inside a wall or the
  floor, off screen, flying away forever, standing in mid-air after a move.
- **Stuck states:** the same pose for many seconds while nothing else happens; a character that never gets up.
- **HUD problems:** lifebar, timer or power bar missing, garbled or overlapping; text drawn in the wrong place.

## Normal in this game (not bugs)

- Hit sparks, big flashes, screen-darkening and coloured backgrounds during supers; brief full-screen flashes.
- "NEW HERO" / "LEVEL" text and the MvC lifebar design; the "CPU LV 5" debug text and small numbers on the right
  side of the screen (engine debug overlay).
- Player 1 flailing, jumping at random and losing: it presses random buttons.
- Characters of very different sizes (Galactus and Thanos are huge on purpose).
- Shadows under characters, motion blur trails *while* a fast move is happening.
- Black bars at the sides of the frame (the game is 4:3 inside a 16:9 recording).

## Already known (report only if it looks different from this)

- Darkseid's hyper background is shifted right and covers the top lifebars (bug 6 in BUGFIX_LIST.md).
- A small stray Thanos portrait on the select screen (8f) — clips don't show the select screen.

## Step 1: screening (contact sheets)

Each sheet holds 6 frames of one clip in time order (left to right, top to bottom), labelled with the clip and time.
Look at every frame and at what changes between frames. **Report anything suspicious, even if unsure** — step 2
filters. Don't report the "normal" items above. For each suspect, give:

```json
{"sheet": "...png", "clip": "...mp4", "time": 12.3, "category": "misplaced effect | leftover | layering |
 broken sprite | position | stuck | hud | other", "what": "one sentence: what you see and where on screen",
 "why": "why it looks like a bug", "confidence": "low | medium | high"}
```

## Step 2: verification (close-up windows)

For each suspect, a window image shows a REFERENCE frame from a few seconds earlier, then every 0.2 s from just
before to well after the moment. Decide whether it is a real engine bug: does it persist or behave in a way no
intended effect would? Could it be a normal super effect or a deliberate pose? Answer:

```json
{"time": 12.3, "clip": "...", "verdict": "bug | not a bug | unsure", "what": "...", "evidence": "which frames show it",
 "guess": "what in the engine might cause it (explod position, palette, layering, state machine...)"}
```
