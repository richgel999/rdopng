# rdopng
Rate-Distortion Optimized Lossy PNG Encoding Tool

rdopng is a command line tool which uses LZ match optimization, a simple perceptual error tolerance model, and [Oklab](https://bottosson.github.io/posts/oklab/)-based colorspace error metrics to encode 24/32bpp PNG files which are 30-80% smaller relative to lodepng/libpng. The tool defaults to reasonably fast near-lossless settings which writes PNG's around 30-40% smaller than lossless PNG encoders.

### Building

Linux (gcc/clang): 

```
cmake .
make
```

Windows (tested with Visual Studio 2022):

```
cmake .
rdopng.sln
```

### Instructions

Encode a .PNG/.BMP/.TGA/.JPG file like this:

```
rdopng file.png
```

Smaller files but 2x slower:

```
rdopng -two_pass file.png
```

Lower than default quality (which is 300), but smaller files:

```
rdopng -lambda 500 file.png
```

Significantly lower quality, using a higher parsing level to compensate for artifacts:

```
rdopng -level 3 -lambda 1000 file.png
```

Enable debug output and write output to Z.png:

```
rdopmng -debug file.png -output z.png
```

Level ranges from 0-29. Levels 0-9 use up to 4 pixel long matches, levels 10-17 use up to 6 pixel long matches, and 18-23 use up to 6 or 12 pixel long matches. Levels 24-29 are impractical (too slow). The higher the level within a match length category, the slower the parsing. The higher match length categories are needed for the higher lambdas/lower bitrates. At near-lossless settings (lower than approximately lambda 300), the smaller/less aggressive parsing levels are usually fine. At higher lambdas/lower bitrates the higher levels are needed to avoid artifacts.

