# AI-2048
Modification of Gariele Cirulli's [2048](https://github.com/gabrielecirulli/2048) to include player and system AI.
[View it on Github Pages.](https://takuyakanbr.github.io/2048)

### Modifying the AI file
In order to speed up execution time, ai.js contains code compiled using Emscripten compiler. Follow the below steps to modify ai.js:
1. Make sure you have [Emscripten](https://kripken.github.io/emscripten-site/index.html) installed.
2. Get [TommyDS](http://www.tommyds.it/) and place its C files in the ```src``` folder. The hashtable provided by TommyDS is used to cache parts of the expectimax search.
3. Make the desired changes in ```src/ai.c```.
4. Compile to ```js/ai.js``` by running the following in the root folder: 
```emcc src/tommy.c src/ai.c -o js/ai.js -O3 --post-js "src/ai.post.js" -s EXPORTED_FUNCTIONS="['_main', '_next_player_move', '_next_system_tile']" --memory-init-file 0```

### License
2048 is licensed under the [MIT license](https://opensource.org/licenses/MIT).
