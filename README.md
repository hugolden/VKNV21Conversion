# NV21 Conversion is implemented in this project. 

As no available NV21 conversion can be found on internet, I tried to implement it on myself and published it here.

Conversion can be found [here](./RenderScriptMigrationSample/app/src/main/cpp/NV21Converter.cpp)

Native interface is also available.

Hope this project is helpful to you

The latter part comes from Google open source project

---



# Android Renderscript Samples

This repository contains a set of individual Android Studio projects:
- [RenderScriptMigrationSample](./RenderScriptMigrationSample): A sample that demonstrates how to migrate RenderScript to Vulkan.
- [BasicRenderScript](./BasicRenderScript): A legacy sample that demonstrates how to use RenderScript to perform some basic image processing.
- [RenderScriptIntrinsic](./RenderScriptIntrinsic): A legacy sample that demonstrates how to use RenderScript intrinsics.

## Renderscript Support Status

Renderscript has been deprecated since Android 12, the samples here are part of the Renderscript deprecation toolkits which also includes:
- [renderscript deprecation blog](https://android-developers.googleblog.com/2021/04/android-gpu-compute-going-forward.html)
- [the migration guide](https://developer.android.com/guide/topics/renderscript/migrate)
- [the migration toolkit library](https://github.com/android/renderscript-intrinsics-replacement-toolkit)


## Support

We highly recommend to use [Stack Overflow](http://stackoverflow.com/questions/tagged/android) to get help from the Andorid community.

If you've found an error in this sample, please file an issue:
https://github.com/android/renderscript-samples

Patches are encouraged, and may be submitted by forking this project and
submitting a pull request through GitHub.

## License

```
Copyright 2021 The Android Open Source Project

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
