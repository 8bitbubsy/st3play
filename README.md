NOTE: This project is not 100% finished! \
I abandoned it several years ago after fighting a bug with the GUS driver, not being able to fix it. \
I eventually rewrote the GUS driver to always run in max-voice mode without quality loss, and the problem may have been fixed.
I don't remember if this is the case, so this player may have some faults.

# st3play
Aims to be an accurate C port of Scream Tracker 3.21's S3M replayer (SB Pro and GUS render mode). \
The project contains example code in the st3play folder on how to interface with the API. \
This is a direct port of the original asm/C source codes.

# Notes
- To compile st3play (the test program) on macOS/Linux, you need SDL2
- The "Sound Blaster Pro" mixer is not the same mixer used in ST3.21. Instead it uses one with higher fidelity and 16-bit sample support
- The code may not be 100% safe to use as a replayer in other projects, and as such I recommend to use this only for reference
