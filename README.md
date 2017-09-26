# Robot Fun Police

Robot Fun Police is Sergio Claure's implementation of [Robot Fun Police](http://graphics.cs.cmu.edu/courses/15-466-f17/game2-designs/jmccann/) for game2 in 15-466-f17.

![Alt text](screenshots/Screenshot.png?raw=true)

## Build Notes

No extra notes to build game

## Asset Pipeline

I used robot.blend provided in the design document for my assets and proceeded to modify export_meshes.py to export_meshes_robot.py to extract the assets from robot.blend into a readable blob

## Architecture

For the most part, I kept the implementation similar to how it was asked in the assignment. In order to create a dependancy between the links, I took the tree stack example and created a similar stack of link that modify one another. In order to manipulate them, I attached events to keys that would multiply the orientation of the respective link to a rotation quaternion (multiplying quaternions leads to an "addition" of both rotations). Unfortunately, I was not able to extract a reliable new position from the links after several rotations (my attempt was to calculate the new position through: new_position = 
quaternion * old_position * conjugate_quaternion). From there, I tried to calculate the distance between the needle and a balloon's center and determine if a collision happened. Because I could not successfully gather the correct distance, I could not implement the proper collision detection and instead mapped the balloons popping to the "w", "e", "r" keys (to show that the animation for popping a balloon still works)

## Reflection

Looking back on the assignment, I believe that calculations regarding quaternions were more difficult than I had anticipated. Likewise, I would have appreciated deeper instructions on setting Blender to the command line path to get the export meshes python script running sooner.


# About Base2

This game is based on Base2, starter code for game2 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng
 - blender (for mesh export script)

On Linux or OSX these requirements should be available from your package manager without too much hassle.
