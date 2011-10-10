libvorbis marmalade
===================

"porting" of libogg and libvorbis to marmalade sdk

Contents
--------
Both libogg and libvorbis library are included (see their README for information about their licenses)
Only few modification to the headers file were done on libvorbis to allow correct compilation.

Included is an example that use libvorbis to decode into memory an ogg file and play it using s3eSound API.
Please note that this is just a proof of concept without any optimization (yet! ;)). 
A lot of code is just copied from the examples included with the vorbis library. 
The ogg file used (named test.ogg)  is  "Epoq - Lepidoptera" and can be downloaded from http://http://www.vorbis.com/music/

The example require a multithreaded anabled enivironment (IOS, Android) and it was successfully tested on a real
android device (Huawei Ideos with Android 2.2)



