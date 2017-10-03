# Robot Fun Police

Cube Volleyball is Sergio Claure's implementation of [Cube Volleyball](http://graphics.cs.cmu.edu/courses/15-466-f17/game3-designs/rmukunda/) for game3 in 15-466-f17.

![Alt text](screenshots/Screenshot.png?raw=true)

## Build Notes

No extra notes to build game

## Asset Pipeline

I used cube_volleyball.blend provided in the design document, along with minor modifications to clean up values (e.g. using 10.0f instead of 9.982489f), for my assets and proceeded to modify export_meshes.py to export_meshes_volley.py to extract the assets from cube_volleyball.blend into a readable blob

## Architecture

For this assignment, I mainly followed the assignment guidelines. I implemented collision detection between the ball and the environment surrounding; I implemented gravity and velocity (while allowing the user to control their characters); and I made the game end after one player was tracked to have 10 points (messages were printed to the command prompt)

## Reflection

Looking back on the assignment, I still had issues trying to implement materials onto my objects and was not able to color my objects.


# About Base2

This game is based on Base2, starter code for game2 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng
 - blender (for mesh export script)

On Linux or OSX these requirements should be available from your package manager without too much hassle.
