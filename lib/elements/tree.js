'use strict';

var inherits = require('inherits');
var Promise  = require('bluebird');

var GameObject = require('../gameobject');

module.exports = Tree;

var cachedTreeGeometry;
var cachedTreeMaterial;

var treeLoader = new Promise(function(resolve) {

  var treeTexture = THREE.ImageUtils.loadTexture('/gamedata/textures/tree.png', THREE.UVMapping, function () {

    var treeAlphaTexture = THREE.ImageUtils.loadTexture('/gamedata/textures/tree.png', THREE.UVMapping, function () {

      var loader = new THREE.OBJLoader();

      loader.load( '/gamedata/elements/tree.obj', function ( object ) {

        object = object.children[0];

        cachedTreeMaterial = object.material;
        cachedTreeMaterial.map = treeTexture;
        cachedTreeMaterial.alphaMap = treeAlphaTexture;
        cachedTreeMaterial.map.needsUpdate = true;
        cachedTreeMaterial.alphaMap.needsUpdate = true;
        cachedTreeMaterial.transparent = true;

        cachedTreeGeometry = object.geometry;
        cachedTreeGeometry.dynamic = false;

        resolve({
          geometry: cachedTreeGeometry,
          material: cachedTreeMaterial
        });
      });
    });
  });
});

/**
 * @exports threearena/elements/Tree
 */
function Tree (options) {

  options = options || {};

  GameObject.apply(this, [ options ]);

  var self = this;

  self.isBlocking = 10.0;

  treeLoader.then(function (template) {

    var object = new THREE.Mesh(template.geometry, template.material);

    object.scale.set(7, 7, 7);

    self.add(object);

    if (options.onLoad) { options.onLoad.apply(self); }

  });

}

inherits(Tree, GameObject);
