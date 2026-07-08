# PreloadMap

Mod that helps remove huge lag spikes by preloading the map.

This mod is mainly aimed at servers with big saves that lag every time you enter an area for the first time since loading in.

It allows you to "visit" every part of the map right after loading. This essentially makes you lag at the start so that you don't lag later.

A mod like this is very useful if you need to race explorers across the map or something.

There are 3 simple settings:

Auto Preload - the option to turn on/off automatic preloading
Radius 2^n [m] - allows you to change the radius of a circle that's rendered, this changes how much lag you get at once, but bigger circles decrease the number of points you have to render. this setting is the n in 2^n, which is how many meters the radius is
Post-stream delay [ms] - changes how long to wait before preloading another point, can help if the server has low disconnect threshold
Startup delay [s] - the time it waits before launching when Auto Preload is ON.
Whole map - this setting renders the whole map at once (not recommended on any bigger map or slower hardware)
The progress is shown in the right corner of your screen with a little widget that disappears when it finishes.

The mod is only on the client side, so you can join any server with it enabled.

You can start/cancel the preloading with simple commands PreloadMap.Cancel and PreloadMap.Start (PreloadMap.Status writes the status in log)
