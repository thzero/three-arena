'use strict';

var _ = require('lodash');
var inherits = require('inherits');

var Character = require('../character');

module.exports = Monsterdog;

/**
 * @exports threearena/character/monsterdog
 *
 * @constructor
 */
function Monsterdog ( options ) {

  var self = this;

  options = _.merge({

    life: 100,
    mana: false,

    radius: 4.0,

    speed: 100,

    name: 'Monsterdog',
    image: '/gamedata/models/monster/portrait.gif',

    onLoad: null,

  }, options);

  Character.apply( this, [ options ]);

  var loader = new THREE.ColladaLoader();
  loader.options.convertUpAxis = true;
  loader.load( '/gamedata/models/monster/monster.dae', function ( collada ) {

    self.character = collada.scene;
    self.skin = collada.skins[ 0 ];

    self.character.traverse(function (child) {
      if (child instanceof THREE.SkinnedMesh) {
        var animation = new THREE.Animation(child, child.geometry.animation);
        animation.play();
      }
    });

    self.skin.geometry.computeFaceNormals();

    self.character.children[0].rotation.y = 180 * Math.PI / 2;
    self.character.children[1].rotation.y = 180 * Math.PI / 2;

    self.character.scale.x = self.character.scale.y = self.character.scale.z = 0.005;
    self.character.updateMatrix();

    self.character.controls = {};
    self.character.setAnimation = function () { };

    self.character.update = function(delta) {
      THREE.AnimationHandler.update(delta / 10);
    };

    self.add(self.character);
    if (options.onLoad) { options.onLoad.apply(self); }
  });
}

inherits(Monsterdog, Character);
