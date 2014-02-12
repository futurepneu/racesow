qfusion - racesow
=======

Source code for the Racesow mod for the Warsow game

Added Features
=============

# Server

## Pure mode

Set `sv_pure` to `2` to skip shaders in pure checks. Useful when having many
maps with repeated and potentially conflicting shaders.

# Client

## Player Trails

Adds a paritcle trail that follow all players. Can be controlled with the
following cvars.

 * `cg_showPlayerTrails` - The partile lifetime in seconds, lower values give
   shorter trails. Disable trails by setting this to `0`.
 * `cg_playerTrailsAlpha` - Initial opacity of the particles. `1.0` is opaque and
   `0` is transparent.
 * `cg_playerTrailsColor` - Color string of the form `"r g b"` where `r`, `g`,
   and `b` are values from 0-1. Set `r`, `g`, or `b` to  `-1` for a random
   color. Set the string to `"1"` to color each player individually.
 * `cg_playerTrailsSize` - Size of the spawned particles.

## Auto demo recording

Will automatically record race demos and name them according to
`gametype/map/map_time_random` on map completion. Control with the following
cvars.

 * `rs_autoRaceDemo` - Set 1 to enable automatic recording, 0 to disable.
 * `rs_autoRaceScreenshot` - Set 1 to automatically take a screenshot at finish
   if `rs_autoRaceDemo` is enabled.

## Other Miscellaneous Options

 * `rs_ignoreTeleEffect` - Set to 1 to disable teleporter particles and sound.

## Additional HUD Variables and cvars

### Hud Cvars

 * `cg_showAcceleration` - sets `SHOW_ACCEL` HUD variable

### Hud Variables

 * `ROCKETACCEL` - Signed speed change when acceleration is above a certain
   threshold. Intended to display speed gained from rocket jumps.
 * `JUMPSPEED` - Speed the player had at the last jump.
 * `MAX_ACCEL` - Theoretical maximum acceleration from strafing. Should be
   compared against `ACCELERATION`.
 * `CP<N>` - Time at the given checkpoint. `<N>` can be a integer from 1 to 15.
