
onmessage = function (e) {
    var g = e.data.grid;
    if (e.data.type == 1) { // system
        var move = Module._next_system_tile(g[0], g[1], g[2], g[3]);
        postMessage({ id: e.data.id, res: move });
    } else { // player
        var move = Module._next_player_move(g[0], g[1], g[2], g[3]);
        postMessage({ id: e.data.id, res: move });
    }
};
