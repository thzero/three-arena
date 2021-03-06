'use strict';

var debug = require('debug')('crowd');
var settings = require('./settings');

var _ = require('lodash');
var now = require('now');
var async = require('async');

module.exports = Crowd;

/**
 * @exports Crowd
 *
 * @constructor
 */
function Crowd (game) {

  var self = this;

  this.game = game;

  this.agents = {};
  this.agentsCount = 0;
  this.updateDurationMax = 0;

  ///////////////

  // this.MAX_VELOCITY_VECTOR = new THREE.Vector3();

  this._tmp_velocity = new THREE.Vector3(0, 0, 0);

  this._originVector = new THREE.Vector3(0, 0, 0);

  this._boundCheckAgents = this._checkAgents.bind(this);
  this._boundCrowdUpdate = this._updateAgents.bind(this);

  this._route_material = new THREE.MeshBasicMaterial({
    color: 0xff0000,
    opacity: 0.5,
    wireframe: true,
    transparent: true
  });

  var update = this.update.bind(this);
  update.listenerTag = 'crowd system';

  // this.game.on('update', update);

  if (self.game.pathfinder) {
    self.game.pathfinder.vent.on('update', self._boundCrowdUpdate);

  } else {
    self.game.on('set:terrain', function(){
      self.game.pathfinder.vent.on('update', self._boundCrowdUpdate);
    });
  }

  settings.on('allCrowdAgentsUpdated', function (allCrowd) {
    for (var i = 0; i < self.agentsCount; i++) {
      self.agents[i].state.separationWeight = allCrowd.crowdSeparationWeight;
      self.agents[i].state.maxAcceleration = allCrowd.crowdMaxAcceleration;
      self.agents[i].state.updateFlags = allCrowd.crowdUpdateFlags;
      self.agents[i].state.maxSpeed = allCrowd.crowdMaxSpeed;
      self.agents[i].state.radius = allCrowd.crowdRadius;
      self.agents[i]._crowd_params_need_update = true;
      self.agents[i]._disabled_behaviours = true;
    }
    debug('all entities have been marked as dirty, naughty agents');
  });
}

Crowd.prototype.attachRouteDebug = function(entity) {

  if (!  entity._crowd_current_route_geometry) {
    entity._crowd_current_route_geometry = new THREE.Geometry();
    entity._crowd_current_route_geometry.vertices.push(this._originVector);
    entity._crowd_current_route_geometry.vertices.push(this._originVector.clone()); // need a new one

    // WHY a new one ??
    var _route_material = new THREE.LineBasicMaterial({
      color: 0xff0000,
      transparent: true,
      opacity: 0.001,
      linewidth: 3,
      vertexColors: THREE.VertexColors
    });

    entity._crowd_current_route_mesh = new THREE.Line(entity._crowd_current_route_geometry, _route_material);
  }

  entity._crowd_current_route_mesh.material.opacity = 0.0001;
  this.game.scene2.add(entity._crowd_current_route_mesh);
};

Crowd.prototype.addAgent = function(entity, options, destination, follow, callback) {

  var self = this;

  entity._crowd_options = crowdOptions(options);

  // add the recast navigation
  self.game.pathfinder.addCrowdAgent(entity._crowd_options, self.game.pathfinder.cb(function(idx){

    self.agentsCount++;

    // keep the idx
    idx = parseInt(idx, 0);
    entity._crowd_idx = idx;
    self.agents[''+idx] = entity;

    self.attachRouteDebug(entity);

    debug('%o assigned to crowd agent #%d', entity, idx);

    function onEntityDestination(destination) {
      entity._crowd_following = null;
      // dont call the worker for a minor change
      if (! entity._crowd_destination ||
        destination.position.distanceTo(entity._crowd_destination.position) > settings.data.crowdMinDestinationChange)
      {

        // Skip if entity uses steerings
        if (entity.steerings.empty()) {
          self.game.pathfinder.crowdRequestMoveTarget(idx, destination.position);
        }

        debug('%o walk towards %o', entity, destination);
      }

      entity._crowd_destination = destination;
    }

    function onEntityFollow(following) {
      entity._crowd_destination = null;
      // dont call the worker for a minor change
      if (!entity._crowd_following || entity._crowd_following !== following ||
        following.position.distanceTo(entity._crowd_following.position) > settings.data.crowdMinDestinationChange)
      {
        entity._crowd_following_last_position = following.position.clone();

        // Skip if entity uses steerings
        if (entity.steerings.empty()) {
          self.game.pathfinder.crowdRequestMoveTarget(idx, following.position);
        }

        debug('%o follows %o', entity, following);
      }

      entity._crowd_following = following;
    }

    function onEntityUnfollow() {
      entity._crowd_destination = null;
      entity._crowd_following = null;
    }

    function onEntityDeath() {
      self.removeAgent(entity);
      entity._crowd_idx = null;

      entity.removeListener('death', onEntityDeath);
      entity.removeListener('follow', onEntityFollow);
      entity.removeListener('destination', onEntityDestination);
      entity.removeListener('nodestination', onEntityUnfollow);
      entity.removeListener('unfollow', onEntityUnfollow);
    }

    entity._crowd_removeAllListeners = function() {
      entity.removeListener('death', onEntityDeath);
      entity.removeListener('follow', onEntityFollow);
      entity.removeListener('destination', onEntityDestination);
      entity.removeListener('nodestination', onEntityUnfollow);
      entity.removeListener('unfollow', onEntityUnfollow);
    };

    // listen on entity events
    entity.on('death', onEntityDeath);
    entity.on('follow', onEntityFollow);
    entity.on('destination', onEntityDestination);
    entity.on('nodestination', onEntityUnfollow);
    entity.on('unfollow', onEntityUnfollow);

    entity._crowd_dirty = false;

    if (typeof callback === 'function') {
      callback(entity);
    }

    entity.emit( destination ? 'destination' : 'nodestination', destination);
    entity.emit( follow ? 'follow' : 'nofollow', follow);
  }));
};

Crowd.prototype.removeAgent = function(entity) {

  if (entity._crowd_current_route_mesh) {
    this.game.scene2.remove(entity._crowd_current_route_mesh);
  }

  if (typeof entity._crowd_idx === 'undefined') {
    throw 'This entity is not in the crowd';
  }

  this.game.pathfinder.removeCrowdAgent(entity._crowd_idx);
  this.agentsCount--;
};

Crowd.prototype.requestMoveVelocity = function(entity, velocity) {

  if (typeof entity._crowd_idx === 'undefined') {
    throw 'This entity is not in the crowd';
  }

  this.game.pathfinder.requestMoveVelocity(entity._crowd_idx, velocity);
};

/**
 * Instantly teleport an entity to a new position
 * @param  {Entity}   entity      Entity to teleport
 * @param  {Vector3}  newPosition The desired new position
 * @param  {Function=} callback   Optional callback(error, newPosition)
 * @return {Entity}               The entity to be teleported
 */
Crowd.prototype.teleport = function(entity, newPosition, callback) {

  var self = this;

  if (typeof entity._crowd_idx === 'undefined') {
    throw 'This entity is not in the crowd';
  }

  entity._crowd_dirty = true;

  entity.game.pathfinder.findNearestPoint(
    newPosition.x,
    newPosition.y,
    newPosition.z,
    1, 2, 1,
    self.game.pathfinder.cb(function(posx, posy, posz){

      entity._crowd_removeAllListeners();

      var options = entity._crowd_options;
      // need to copy because nearestPosition is pooled
      options.position.x = posx;
      options.position.y = posy;
      options.position.z = posz;

      var following = null; // entity._crowd_following;
      var destination = null; // entity._crowd_destination;

      self.removeAgent(entity);
      self.addAgent(entity, options, destination, following, callback);

      entity._crowd_dirty = false;
      })
  );
};

Crowd.prototype.update = function() {

  var self = this;

  if (this.updateTime && self._stillUpdating) {
    debug('Crowd.update has been called twice !');

    if (now() - this.updateTime < 100) {
      debug('but it was a long time ago');

    } else {
      return;
    }
  }

  self._stillUpdating = true;

  if (self.agentsCount > 0) {

    // update routes visibility
    this._route_material.visible = settings.data.visibleCharactersRoutes;

    // check all agents state
    _.forEach(self.agents, this._boundCheckAgents);

    this.updateTime = now();
    this.updateDuration = null;

    // update the crowd
    self.game.pathfinder.crowdUpdate(this.game.delta * 2 /* FIXME: why ? */); // , this._boundCrowdUpdate);
  }
};

Crowd.prototype._checkAgents = function(entity, idx) {

  if (entity.isDead()) {
    return;
  }

  if (entity.state.isStatic) {
    return;
  }

  idx = parseInt(idx, 0);

  // update dirty params
  if (entity._crowd_params_need_update) {
    debug('update crowd params for %o', entity);
    entity._crowd_params_need_update = false;
    entity._crowd_dirty = false;
    entity._disabled_behaviours = false;

    // this.game.pathfinder.updateCrowdAgentParameters(idx, crowdOptions(entity.state));
    var oldFoll = null; // entity._crowd_following;
    var oldDest = null; // entity._crowd_destination;

    this.removeAgent(entity);
    this.addAgent(entity, entity.state, oldDest, oldFoll);
    return;
  }

  // use steering
  if (! entity.steerings.empty()) {
    entity.applySteerings();
  }

  // update dirty follow
  // TODO: also use a _dirtyMove flag ?
  if (entity._crowd_following) {
    // dont call the worker for a minor change
    if (entity._crowd_following_last_position.distanceTo(entity._crowd_following.position) > 2.0 * settings.data.crowdMinDestinationChange)
    {
      this.game.pathfinder.crowdRequestMoveTarget(idx, entity._crowd_following.position);
      entity._crowd_following_last_position.copy(entity._crowd_following.position);
    }
  }
};

Crowd.prototype._updateAgent = function(agent){

  // var agent = agents[i];
  var idx = agent.idx;
  var entity = this.agents[idx];

  if (! entity) {
    // console.warning('missing entity ?!');
    return;
  }

  if (entity._crowd_idx && (entity._crowd_dirty || ! agent.active || entity.isDead())) {
    this.removeAgent(entity);
    return;
  }

  if (entity.state.isStatic) {
    return;
  }

  var destination = entity._crowd_destination || entity._crowd_following;
  var destinationDistance = Number.Infinity;

  if (destination) {

    destinationDistance = destination.position.distanceTo(entity.position) -
      (destination.state && destination.state.radius ? destination.state.radius : 0) -
      (entity.state && entity.state.radius ? entity.state.radius : 0);

    // check if agent is arrived
    // could be done in recastnavigation, but not all entities are agents :/
    if (destinationDistance <= 0) {

      agent.velocity.x = agent.velocity.y = agent.velocity.z = 0.0;
      agent.position = entity.position;

    } else {

      var mul = 1;

      agent.velocity.x *= destinationDistance / mul;
      agent.velocity.y *= destinationDistance / mul;
      agent.velocity.z *= destinationDistance / mul;
    }

  }

  // update routes
  if (entity._crowd_current_route_geometry) {
    // defaults
    entity._crowd_current_route_mesh.material.opacity = 0.0001;

    if (settings.data.visibleCharactersRoutes) {
      entity._crowd_current_route_geometry.vertices[1].copy(entity.position);
      entity._crowd_current_route_geometry.vertices[0].copy(entity.position);
      entity._crowd_current_route_geometry.verticesNeedUpdate = true;

      if (destination && ! entity._crowd_dirty) {
        entity._crowd_current_route_mesh.material.opacity = (destinationDistance > 10 ? 0.5 : 0.001);
        entity._crowd_current_route_geometry.vertices[1].copy(destination.position);
      }
    }
  }

  this._tmp_velocity.set(agent.velocity.x, 0, agent.velocity.z);

  var speed = this._tmp_velocity.length();

  entity.isMoving = speed > 0;

  // update back entity position & rotation
  entity.position.copy(agent.position);

  // Animations
  if (speed > settings.data.crowdMinSpeedAnimation) {

    if (entity.character) {
      entity.character.controls.moveForward = true;
      entity.character.setAnimation('run');
    }

  } else {

    if (entity.character) {
      entity.character.controls.moveForward = false;
      entity.character.setAnimation('stand');
    }
  }

  if (speed > settings.data.crowdMinSpeedRotation) {
    var angle = Math.atan2(- this._tmp_velocity.z, this._tmp_velocity.x);
    entity.rotation.y = angle;
  }
};

Crowd.prototype._updateAgents = function(agents){

  this.updateDuration = now() - this.updateTime;

  for (var a = 0; a < agents.length; a++) {
    this._updateAgent(agents[a]);
  }

  if (this.updateDurationMax < this.updateDuration) {
    this.updateDurationMax = this.updateDuration;
    debug('pathfinder update took %oms', this.updateDuration);
  }

  this._stillUpdating = false;
};

function crowdOptions(options) {
  options = _.merge({
    position: { x:0, y:0, z:0 },
    separationWeight: settings.data.crowdDefaultSeparationWeight,
    maxAcceleration: settings.data.crowdDefaultMaxAcceleration,
    updateFlags: settings.data.crowdDefaultUpdateFlags,
    maxSpeed: settings.data.crowdDefaultMaxSpeed,
    radius: settings.data.crowdDefaultRadius,
    height: settings.data.crowdDefaultHeight
  }, {
    position: options.position,
    separationWeight: options.separationWeight,
    maxAcceleration: options.maxAcceleration,
    updateFlags: options.updateFlags,
    maxSpeed: options.maxSpeed,
    radius: options.radius,
    height: options.height
  });

  // double check positions
  options.position = {
    x: options.position.x || 0,
    y: options.position.y || 0,
    z: options.position.z || 0
  };

  return options;
}
