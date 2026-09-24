<?php

/**
 * @file
 * Base class for ECK entities.
 */

/**
 * Base class for ECK entities.
 */
class EckEntity extends Entity {

  /**
   * ID
   *
   * @var int
   */
  public $id;

  /**
   * Entity Type.
   *
   * @var string
   */
  public $entity_type;

  /**
   * Type / Bundle.
   *
   * @var string
   */
  public $type;

  /**
   * Implements EntityInterface::id().
   */
  public function id() {
    return isset($this->id) ? $this->id : NULL;
  }

  /**
   * Implements EntityInterface::label().
   */
  public function label() {
    return eck__entity__label($this);
  }

  /**
   * Implements EntityInterface::bundle().
   */
  public function bundle() {
    return $this->type ?? $this->entityType();
  }

  /**
   * Implements EntityInterface::entityType().
   */
  public function entityType() {
    return $this->entity_type;
  }

  /**
   * Implements EntityInterface::uri()
   */
  public function uri() {
    return eck__entity__uri($this);
  }

}
