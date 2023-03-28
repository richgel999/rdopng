# rdopng
Rate-Distortion Optimized Lossy PNG, QOI, and LZ4 image (LZ4I) Encoding Tool

rdopng is a command line tool which uses LZ match optimization, Lagrangian multiplier [rate distortion optimization (RDO)](https://en.wikipedia.org/wiki/Rate%E2%80%93distortion_optimization), a simple perceptual error tolerance model, and [Oklab](https://bottosson.github.io/posts/oklab/)-based colorspace error metrics to encode lossy 24/32bpp PNG/QOI/LZ4I files. The encoded lossy PNG files are typically 30-80% smaller relative to lodepng/libpng. The tool defaults to reasonably fast near-lossless settings which writes PNG's around 30-40% smaller than lossless PNG encoders.

Unlike [pngquant](https://pngquant.org/), rdopng does not use 256-color palettes or dithering. PNG files encoded by rdopng typically range between roughly 2.5-7bpp, depending on the options used (and how much time and patience you have).

Some example encodes and command lines are [here](https://github.com/richgel999/rdopng/wiki/Examples).

You can download a pre-built Windows binary for an older version of rdopng [here](https://github.com/richgel999/rdopng/releases). (The latest version is in the repo.) You may need to install the [VS 2022 runtime redistributable from Microsoft](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170). 

### Building

You'll need [cmake](https://cmake.org/). There are no other dependencies.

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

Encodes a .PNG/.BMP/.TGA/.JPG file to "./file_rdo.png":

```
rdopng file.png
```

Encodes a .PNG/.BMP/.TGA/.JPG file to "./file_rdo.qoi" (and also unpacks the coded image and saves it as .PNG):

```
rdopng -qoi -unpack_qoi_to_png file.png 
```

Encodes a file to "./file_rdo.qoi" at higher quality per bit, but much slower (also try -better which is in between the default/uber settings):

```
rdopng -qoi -uber -unpack_qoi_to_png file.png 
```

Encodes smaller PNG files but will be 2x slower:

```
rdopng -two_pass file.png
```

Encodes at lower than default quality (which is 300), but writes smaller files:

```
rdopng -lambda 500 file.png
```

Significantly lower PNG quality (which increases artifacts), using a higher than default parsing level to compensate for artifacts:

```
rdopng -level 3 -lambda 1000 file.png
```

Enable debug output and write output to z.png:

```
rdopng -debug file.png -output z.png
```

Load a normal map, normalize it, pack it using angular normal map metrics, decoded/encode texels using GPU SNORM unpacking (instead of the default UNORM):

```
rdopng -normalize -normal_map -snorm file.png
```

Level ranges from 0-29. Levels 0-9 use up to 4 pixel long matches, levels 10-17 use up to 6 pixel long matches, and 18-23 use up to 6 or 12 pixel long matches. Levels 24-29 use exhaustive matching and are beyond impractical except on tiny images. 

The higher the level within a match length category, the slower the encoder. Higher match length categories are needed for the higher lambdas/lower bitrates. At near-lossless settings (lower than approximately lambda 300), the smaller/less aggressive parsing levels are usually fine. At higher lambdas/lower bitrates the higher levels are needed to avoid artifacts. To get below roughly 3-4bpp you'll need to use high lambdas, two pass mode, and very slow parsing levels.

-lambda is the quality slider. Useful lambda values are roughly 1-20000, but values beyond approximately 500-1000 (depending on the image) will require fiddling with the level to compensate for artifacts. Higher levels are extremely slow because the current tool is single threaded.

Most options work with both QOI, LZ4I and PNG. The -level option is only for PNG, and the -uber/-better options are only for QOI/LZ4I.

### RDO LZ4 examples

```
rdopng -lz4i -lambda 5000 -debug -better file.png
```

Unpacking .LZ4I images to PNG:

```
rdopng -unpack file.lz4i
```

LZ4I image files contain a simple header followed by the RGB(A) pixels compressed using LZ4. Here's the header (it's like QOI's but with a different sig):

```
#pragma pack(push, 1)
struct lz4i_header
{
	char sig[4]; // signature bytes "lz4i"
	uint32_t width; // image width in pixels (BE)
	uint32_t height; // image height in pixels (BE)
	uint8_t channels; // 3 = RGB, 4 = RGBA
	uint8_t colorspace; // 0 = sRGB with linear alpha 1 = all channels linear
};
#pragma pack(pop)
```

### Known Problems
rdopng has only been tested on little endian platforms, under Windows using MSVC and Ubuntu Linux using clang/gcc. There are a few known endian issues in there, which I'll eventually fix. It has not been compiled or tested on OSX.

### Special Thanks
Thanks to [Paul Hughes](https://twitter.com/PaulieHughes) for encouraging me to continue working on this on Twitter. Also, thanks to [Jyrki Alakuijala](https://twitter.com/jyzg) for suggesting to drop YCbCr for an alternative such as Oklab.

