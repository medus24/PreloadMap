# PreloadMap

This mod is mainly aimed at servers with big saves that lag every time you enter an area for the first time since loading in. 

This mod allows you to "visit" every part of the map right after loading. 
This essentially makes you lag at the start so that you don't lag later.

Such mod is very useful if you need to race explorers across the map or something.

There are 3 simple settings:

1. Auto Preload - the option to turn on/off automatic preloading
2. Radius 2^n [m] - this is a bit more complex setting, which allows you to change the radius of a circle that's rendered, this changes how much lag you get at once, but higher numbers increase the number of points you have to render. this setting is the n in 2^n, which is how many meters the radius is (you control the power of 2).
3. Whole map - this setting renders the whole map at once (not recommended on any bigger map)

The progress is shown in the right corner of your screen with a little widget that disappears when it finishes.

You can start/cancel the preloading with simple commands PreloadMap.Cancel and PreloadMap.Start
(PreloadMap.Status writes the status in log)
