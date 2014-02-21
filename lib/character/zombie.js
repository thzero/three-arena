'use strict';

var _ = require('lodash');
var inherits = require('inherits');

var Character = require('../character');

module.exports = Zombie;

/**
 * @exports threearena/character/zombie
 * 
 * @constructor
 */
function Zombie ( options ) {

  var self = this;

  options = _.merge({

    life: 100,
    mana: false,

    radius: 4.0,

    speed: 100,

    name: 'Zombie',
    image: '/gamedata/models/monster/portrait.gif',

    onLoad: null,

  }, options);

  Character.apply( this, [ options ]);

  var loader = new THREE.ColladaLoader();
  loader.options.convertUpAxis = true;
  loader.load( '/gamedata/nonfree/zombie_injured_walk/FREE__injured_walk.dae', function ( collada ) {

    self.character = collada.scene;
    self.skin = collada.skins[ 0 ];

    self.character.meshBody = self.character.children[1];

    self.skin.geometry.computeFaceNormals();

    // self.character.children[0].rotation.y = 180 * Math.PI / 2;
    self.character.children[1].rotation.y = 45 * Math.PI / 2;

    self.character.scale.x = self.character.scale.y = self.character.scale.z = 0.08;
    self.character.updateMatrix();

    self.character.children[1].material.map = THREE.ImageUtils.loadTexture( '/gamedata/nonfree/zombie_injured_walk/textures/ZombieLowRes_diffuse.png' );
    self.character.children[1].material.specularMap = THREE.ImageUtils.loadTexture( '/gamedata/nonfree/zombie_injured_walk/textures/Zombie_specular.png' );

    self.character.controls = {};
    self.character.setAnimation = function () { };

    var t = 0;
    self.character.update = function(delta) {
      if ( t > 1 ) { t = 0; }

      // guess this can be done smarter...
      // (Indeed, there are way more frames than needed and interpolation is not used at all
      //  could be something like - one morph per each skinning pose keyframe, or even less,
      //  animation could be resampled, morphing interpolation handles sparse keyframes quite well.
      //  Simple animation cycles like this look ok with 10-15 frames instead of 100 ;)

      for ( var i = 0; i < self.skin.morphTargetInfluences.length; i++ ) {
        self.skin.morphTargetInfluences[ i ] = 0;
      }

      self.skin.morphTargetInfluences[ Math.floor( t * 30 ) ] = 1;

      t += delta;
    };

    self.add(self.character);
    if (options.onLoad) { options.onLoad.apply(self); }
  });
}

inherits(Zombie, Character);