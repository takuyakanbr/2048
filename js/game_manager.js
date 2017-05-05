function GameManager(size, InputManager, Actuator, StorageManager) {
  this.size           = size; // Size of the grid
  this.inputManager   = new InputManager;
  this.storageManager = new StorageManager;
  this.actuator       = new Actuator;

  this.id = 1;
  this.startTiles = 2;
  this.playerAI = false;
  this.opponentAI = false;
  this.waiting = false;

  this.inputManager.on("player", this.updatePlayer.bind(this));
  this.inputManager.on("opponent", this.updateOpponent.bind(this));
  this.inputManager.on("move", this.move.bind(this));
  this.inputManager.on("restart", this.restart.bind(this));
  this.inputManager.on("keepPlaying", this.keepPlaying.bind(this));

  this.setup();
  this.setupWorkers();
}

GameManager.prototype.setupWorkers = function () {
    var self = this;
    this.workerP = new Worker('js/ai.js');
    this.workerP.onmessage = function (e) {
        if (e.data.id != self.id) return;
        self.move(e.data.res); // make the desired move
    };
    this.workerOp = new Worker('js/ai.js');
    this.workerOp.onmessage = function (e) {
        if (e.data.id != self.id) return;
        self.addTileAt(e.data.res); // add the tile at the desired position
    };
};

var Log2 = {
    0: 0, 2: 1, 4: 2, 8: 3, 16: 4, 32: 5, 64: 6, 128: 7, 256: 8, 512: 9,
    1024: 10, 2048: 11, 4096: 12, 8192: 13, 16384: 14, 32768: 15
};
var Positions = [[3, 3], [2, 3], [1, 3], [0, 3], [3, 2], [2, 2], [1, 2], [0, 2],
[3, 1], [2, 1], [1, 1], [0, 1], [3, 0], [2, 0], [1, 0], [0, 0]];

// return an array of 4 numbers, representing the tiles in each row
GameManager.prototype.convertGrid = function () {
    var bn = [0, 0, 0, 0];
    var mat = this.grid.cells;
    for (var r = 0; r < 4; r++) {
        for (var c = 0; c < 4; c++) {
            bn[r] <<= 4;
            var tile = mat[c][r];
            if (tile !== null) {
                bn[r] |= Log2[tile.value];
            }
        }
    }
    return bn;
};

// ask the worker for the next player move
GameManager.prototype.doPlayerMove = function () {
    if (!this.waiting && !this.over)
        this.workerP.postMessage({ id: this.id, type: 0, grid: this.convertGrid() });
};

// ask the worker for the next system tile position
GameManager.prototype.doOpponentMove = function () {
    if (!this.over)
        this.workerOp.postMessage({ id: this.id, type: 1, grid: this.convertGrid() });
};

// set / unset the use of player AI
GameManager.prototype.updatePlayer = function (useAI) {
    this.playerAI = useAI == 1;
    if (this.playerAI) this.doPlayerMove();
};

// set / unset the use of system AI
GameManager.prototype.updateOpponent = function (useAI) {
    this.opponentAI = useAI == 1;
};

// Adds a tile at a specific position
GameManager.prototype.addTileAt = function (res) {
    var value = Math.random() < 0.9 ? 2 : 4;
    var pos = Positions[res];
    var tile = new Tile({ x: pos[0], y: pos[1] }, value);

    this.grid.insertTile(tile);
    this.waiting = false;

    if (!this.movesAvailable()) {
        this.over = true; // Game over!
    }

    this.actuate();
    if (this.playerAI) this.doPlayerMove();
};


// Restart the game
GameManager.prototype.restart = function () {
  this.id++;
  this.waiting = false;
  this.storageManager.clearGameState();
  this.actuator.continueGame(); // Clear the game won/lost message
  this.setup();
  if (this.playerAI) this.doPlayerMove();
};

// Keep playing after winning (allows going over 2048)
GameManager.prototype.keepPlaying = function () {
  this.actuator.continueGame(); // Clear the game won/lost message
};

// Return true if the game is lost
GameManager.prototype.isGameTerminated = function () {
  return this.over;
};

// Set up the game
GameManager.prototype.setup = function () {
  var previousState = this.storageManager.getGameState();

  // Reload the game from a previous game if present
  if (previousState) {
    this.grid        = new Grid(previousState.grid.size,
                                previousState.grid.cells); // Reload grid
    this.score       = previousState.score;
    this.over        = previousState.over;
    this.won         = previousState.won;
    this.keepPlaying = previousState.keepPlaying;
  } else {
    this.grid        = new Grid(this.size);
    this.score       = 0;
    this.over        = false;
    this.won         = false;
    this.keepPlaying = false;

    // Add the initial tiles
    this.addStartTiles();
  }

  // Update the actuator
  this.actuate();
};

// Set up the initial tiles to start the game with
GameManager.prototype.addStartTiles = function () {
  for (var i = 0; i < this.startTiles; i++) {
    this.addRandomTile();
  }
};

// Adds a tile in a random position
GameManager.prototype.addRandomTile = function () {
  if (this.grid.cellsAvailable()) {
    var value = Math.random() < 0.9 ? 2 : 4;
    var tile = new Tile(this.grid.randomAvailableCell(), value);

    this.grid.insertTile(tile);
  }
};

// Sends the updated grid to the actuator
GameManager.prototype.actuate = function () {
  if (this.storageManager.getBestScore() < this.score) {
    this.storageManager.setBestScore(this.score);
  }

  // Clear the state when the game is over (game over only, not win)
  if (this.over) {
    this.storageManager.clearGameState();
  } else {
    this.storageManager.setGameState(this.serialize());
  }

  this.actuator.actuate(this.grid, {
    score:      this.score,
    over:       this.over,
    won:        this.won,
    bestScore:  this.storageManager.getBestScore(),
    terminated: this.isGameTerminated()
  });

};

// Represent the current game as an object
GameManager.prototype.serialize = function () {
  return {
    grid:        this.grid.serialize(),
    score:       this.score,
    over:        this.over,
    won:         this.won,
    //keepPlaying: this.keepPlaying
  };
};

// Save all tile positions and remove merger info
GameManager.prototype.prepareTiles = function () {
  this.grid.eachCell(function (x, y, tile) {
    if (tile) {
      tile.mergedFrom = null;
      tile.savePosition();
    }
  });
};

// Move a tile and its representation
GameManager.prototype.moveTile = function (tile, cell) {
  this.grid.cells[tile.x][tile.y] = null;
  this.grid.cells[cell.x][cell.y] = tile;
  tile.updatePosition(cell);
};

// Move tiles on the grid in the specified direction
// 0: up, 1: right, 2: down, 3: left
GameManager.prototype.move = function (direction) {
  if (this.waiting) return;

  var self = this;

  if (this.isGameTerminated()) return; // Don't do anything if the game's over

  var cell, tile;

  var vector     = this.getVector(direction);
  var traversals = this.buildTraversals(vector);
  var moved      = false;

  // Save the current tile positions and remove merger information
  this.prepareTiles();

  // Traverse the grid in the right direction and move tiles
  traversals.x.forEach(function (x) {
    traversals.y.forEach(function (y) {
      cell = { x: x, y: y };
      tile = self.grid.cellContent(cell);

      if (tile) {
        var positions = self.findFarthestPosition(cell, vector);
        var next      = self.grid.cellContent(positions.next);

        // Only one merger per row traversal?
        if (next && next.value === tile.value && !next.mergedFrom) {
          var merged = new Tile(positions.next, tile.value * 2);
          merged.mergedFrom = [tile, next];

          self.grid.insertTile(merged);
          self.grid.removeTile(tile);

          // Converge the two tiles' positions
          tile.updatePosition(positions.next);

          // Update the score
          self.score += merged.value;

          // The mighty 2048 tile
          if (merged.value === 2048) self.won = true;
        } else {
          self.moveTile(tile, positions.farthest);
        }

        if (!self.positionsEqual(cell, tile)) {
          moved = true; // The tile moved from its original cell!
        }
      }
    });
  });

  if (moved) {
      var cells = this.grid.availableCells().length;
      if (!this.opponentAI || cells <= 1) {
          this.addRandomTile();
          if (!this.movesAvailable()) {
              this.over = true; // Game over!
          }
          this.actuate();
          if (this.playerAI) this.doPlayerMove();
      } else {
          this.waiting = true;
          this.doOpponentMove();
      }
  }
};

// Get the vector representing the chosen direction
GameManager.prototype.getVector = function (direction) {
  // Vectors representing tile movement
  var map = {
    0: { x: 0,  y: -1 }, // Up
    1: { x: 1,  y: 0 },  // Right
    2: { x: 0,  y: 1 },  // Down
    3: { x: -1, y: 0 }   // Left
  };

  return map[direction];
};

// Build a list of positions to traverse in the right order
GameManager.prototype.buildTraversals = function (vector) {
  var traversals = { x: [], y: [] };

  for (var pos = 0; pos < this.size; pos++) {
    traversals.x.push(pos);
    traversals.y.push(pos);
  }

  // Always traverse from the farthest cell in the chosen direction
  if (vector.x === 1) traversals.x = traversals.x.reverse();
  if (vector.y === 1) traversals.y = traversals.y.reverse();

  return traversals;
};

GameManager.prototype.findFarthestPosition = function (cell, vector) {
  var previous;

  // Progress towards the vector direction until an obstacle is found
  do {
    previous = cell;
    cell     = { x: previous.x + vector.x, y: previous.y + vector.y };
  } while (this.grid.withinBounds(cell) &&
           this.grid.cellAvailable(cell));

  return {
    farthest: previous,
    next: cell // Used to check if a merge is required
  };
};

GameManager.prototype.movesAvailable = function () {
  return this.grid.cellsAvailable() || this.tileMatchesAvailable();
};

// Check for available matches between tiles (more expensive check)
GameManager.prototype.tileMatchesAvailable = function () {
  var self = this;

  var tile;

  for (var x = 0; x < this.size; x++) {
    for (var y = 0; y < this.size; y++) {
      tile = this.grid.cellContent({ x: x, y: y });

      if (tile) {
        for (var direction = 0; direction < 4; direction++) {
          var vector = self.getVector(direction);
          var cell   = { x: x + vector.x, y: y + vector.y };

          var other  = self.grid.cellContent(cell);

          if (other && other.value === tile.value) {
            return true; // These two tiles can be merged
          }
        }
      }
    }
  }

  return false;
};

GameManager.prototype.positionsEqual = function (first, second) {
  return first.x === second.x && first.y === second.y;
};
