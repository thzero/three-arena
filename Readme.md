Three Arena
===

Three Arena is an opiniated WebGL game framework to create 3D hack and slash games in an HTML context. It uses [three.js](http://threejs.org) 3D engine, [machinejs](http://machinejs.maryrosecook.com) behaviour trees, [recastnavigation](https://github.com/memononen/recastnavigation) pathfinding system, [knockoutjs](http://knockoutjs.com) dom binding system, and [other](three-arena/blob/master/bower.json) open source projects.



Demo
===
![Current status](https://raw.github.com/vincent/three-arena/master/app/images/screenshots/2013-09-09.png)

http://ec2-54-228-33-93.eu-west-1.compute.amazonaws.com:9000

Examples
===
```js

new Arena({

  container: 'game-container', // the container DOM ID

  showRoutes: true // show characters route paths
})

.setTerrain('/gamedata/maps/simplest.obj', { // use this .OBJ as terrain

  tDiffuse: '/gamedata/textures/plain_blue.png' // the terrain texture
})

.addCharacter(function(done){ // add a character

  new Ogro({

    name: 'Shrek', // the character name

    image: '/gamedata/unknown.png', // its portrait

    tomb: '/gamedata/models/rts_elements.dae', // use this model when it dies

    life: 100, // start with 100 life points

    onLoad: function(){

      this.learnSpell(FireBulletSpell); // learn a spell

      done(this); // on scene !
    }
  });
  
});
```

Show me the code
===

* The main game codebase is in the [threearena directory](app/scripts/threearena)
* Libraries used
 - RequireJS
 - Lo-Dash
 - Async
 - ThreeJS (r60), TweenJS, Sparks
 - Knockout
 - jQuery (only when really useful)
