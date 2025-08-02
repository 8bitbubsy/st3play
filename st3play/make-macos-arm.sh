#!/bin/bash

echo Compiling arm64 binary, please wait...

rm release/other/st3play &> /dev/null

clang -target arm64-apple-macos11 -mmacosx-version-min=11.0 -arch arm64 -march=armv8.3-a+sha3 -I/Library/Frameworks/SDL2.framework/Headers -F/Library/Frameworks -g0 -DNDEBUG -DAUDIODRIVER_SDL .../audiodrivers/sdl/*.c ../*.c src/*.c -O3 -lm -Winit-self -Wno-deprecated -Wextra -Wunused -mno-ms-bitfields -Wno-missing-field-initializers -Wswitch-default -framework SDL2 -framework Cocoa -lm -o release/other/st3play
strip release/other/st3play
install_name_tool -change @rpath/SDL2.framework/Versions/A/SDL2 @executable_path/../Frameworks/SDL2.framework/Versions/A/SDL2 release/other/st3play

rm ../*.o src/*.o &> /dev/null
echo Done. The executable can be found in \'release/other\' if everything went well.
