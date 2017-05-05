# AI-2048
Modification of Gariele Cirulli's [2048](https://github.com/gabrielecirulli/2048) to include player and system AI.

### Modifying the AI file
In order to speed up execution time, ai.js contains code compiled using Emscripten compiler. Follow the below steps to modify ai.js:
1. Make sure you have [Emscripten](https://kripken.github.io/emscripten-site/index.html) installed.
2. Get [TommyDS](http://www.tommyds.it/) and place its C files in the same folder as ai.c. The hashtable provided by TommyDS is used to cache parts of the expectimax search.
3. Make the desired changes in ai.c.
4. Compile to ai.js using 
```emcc tommy.c ai.c -o ai.js -O3 --post-js "ai.post.js" -s EXPORTED_FUNCTIONS="['_main', '_next_player_move', '_next_system_tile']" --memory-init-file 0```

### License
2048 is licensed under the [MIT license](https://opensource.org/licenses/MIT).
