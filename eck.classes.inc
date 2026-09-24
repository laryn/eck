<?php

/**
 * @file
 * Classes for all the different objects used in ECK.
 */

/**
 * An entity type database object.
 */
class EckEntityType {

  // If an entity type is new, we can create its table from the current data of
  // the object, but if this is a loaded object, we need to actually keep
  // track of the changes happening so we can modify the already existing table
  // appropiately.
  public $changes;

  /**
   * An array of properties for the entity type.
   *
   * @var array
   */
  public $properties = [];

  /**
   * The human-readable label of the entity type.
   *
   * @var string
   */
  public $label;

  /**
   * The machine name of the entity type.
   *
   * @var string
   */
  public $name;

  /**
   * Indicates whether the entity type is new.
   *
   * @var bool
   */
  public $isNew = TRUE;

  /**
   * Constructor.
   */
  public function __construct() {
    $this->properties = array();
    $this->changes = array();
  }

  /**
   * @param string[] $names
   * @return EckEntityType[]
   */
  public static function loadMultipleByName(array $names) {
    $entityTypes = array();

    foreach ($names as $name) {
      $entityType = self::loadByName($name);
      if (!empty($entityType)) {
        $entityTypes[$name] = $entityType;
      }
    }

    return $entityTypes;
  }

  /**
   * Add a new property to the entity type.
   *
   * @param string $name
   *   The name.
   * @param string $label
   *   A label.
   * @param string $type
   *   The type of the property.
   * @param string $behavior
   *   A behavior to attach to the property.
   */
  public function addProperty($name, $label, $type, $behavior = NULL) {
    if (!$this->isNew) {
      $this->recordFieldChange('add', $name);
    }

    $p = $p2 = $this->properties;
    // @todo check that type is an actual type.
    $p[$name] = array(
      'label' => $label,
      'type' => $type,
      'behavior' => $behavior,
    );

    $this->properties = $p;

    // If the property did not exist already, let the behaviors execute some
    // code.
    if (!array_key_exists($name, $p2)) {
      eck_property_behavior_invoke_plugin($this, 'property_add',
        array(
          'name' => $name,
          'property' => $p[$name],
          'entity_type' => $this,
        ));
    }
  }

  /**
   * Remove a property.
   *
   * @param string $name
   *   The name of the property.
   */
  public function removeProperty($name) {
    $p = $this->properties;
    if (array_key_exists($name, $p)) {

      // Let the behaviors execute some code.
      eck_property_behavior_invoke_plugin($this, 'property_remove',
        array(
          'name' => $name,
          'property' => $p[$name],
          'entity_type' => $this,
        ));

      unset($p[$name]);
      $this->properties = $p;
      if (!$this->isNew) {
        $this->recordFieldChange('remove', $name);
      }
    }
  }

  /**
   * Change the behavior of a property.
   *
   * @param string $name
   *   The name of the property.
   * @param string $behavior
   *   The name of the behavior.
   */
  public function changeBehavior($name, $behavior) {
    $p = $this->properties;
    // @todo check that type is an actual type.
    if (array_key_exists($name, $p)) {
      $p[$name]['behavior'] = $behavior;
      // @todo look at this more closelly, does the behavior change really
      // affect the property cache?
      entity_property_info_cache_clear();
    }
    else {
      // @todo add exception if the property does not exist.
    }

    $this->properties = $p;
  }

  /**
   * Remove behavior.
   *
   * @param string $name
   *   The name of the behavior.
   */
  public function removeBehavior($name) {
    $this->changeBehavior($name, NULL);
  }

  /**
   * Keep track of changes happening to the entity type.
   *
   * This is useful to later replica the changes on the DB.
   */
  private function recordFieldChange($op, $name) {
    // If it is not new we need to keep track of stuff.
    if (!$this->isNew) {
      $p = $this->properties;
      $c = $this->changes;
      switch ($op) {
        case 'add':
          // If the property does not exist already add keep track.
          if (!array_key_exists($name, $p)) {
            $c[$op][] = $name;
          }
          break;

        case 'remove':
          // If there is an add in the changes take it out, otherwise add a
          // remove.
          if (array_key_exists('add', $c)) {
            $key = array_search($name, $c['add']);
            if ($key != FALSE) {
              unset($c['add'][$key]);
            }
          }
          else {
            $c[$op][] = $name;
          }
          break;

      }
      $this->changes = $c;
    }
  }

  /**
   * Load entity type.
   */
  public function load($name) {
    $config = config('eck.entity_type.' . $name);
    if (!empty($config) && !$config->isNew()) {
      foreach ($config->get() as $property => $value) {
        $this->{$property} = $value;
      }
      $this->isNew = FALSE;
    }
  }

  /**
   * Save.
   */
  public function save() {
    if ($this->isNew) {
      module_load_include('inc', 'eck', 'eck.entity_type');
      $schema = eck__entity_type__schema($this);
      db_create_table("eck_{$this->name}", $schema);

      // Create the new entity cache table for this entity.
      $schema = backdrop_get_schema_unprocessed('system', 'cache');
      $schema['description'] = "Cache table used to store {$this->name} entity records.";
      db_create_table("cache_entity_{$this->name}", $schema);

      // Allow other modules to respond to the creation of entity types.
      module_invoke_all('eck_entity_type_insert', $this);

    }
    else {
      // Modify the already existing table in accordance with the
      // recorded changes.
      if (array_key_exists('add', $this->changes)) {
        foreach ($this->changes['add'] as $name) {
          // First lets get the record.
          $properties = $this->properties;
          $property = $properties[$name];

          // Now we check to see whether it is a default or a custom property
          // it is not custom so lets get the schema and add the field.
          $schema = eck_property_type_schema($property['type']);
          db_add_field("eck_{$this->name}", $name, $schema);
        }
      }
      if (array_key_exists('remove', $this->changes)) {
        foreach ($this->changes['remove'] as $name) {
          db_drop_field("eck_{$this->name}", $name);
        }
      }

      // Allow other modules to respond to the change of entity types.
      module_invoke_all('eck_entity_type_update', $this);
    }

    $config = config('eck.entity_type.' . $this->name);
    $config->set('name', $this->name);
    $config->set('label', $this->label);
    $config->set('properties', $this->properties);
    $config->save();

    global $_eck_entity_types_cache;

    $cache_enabled = isset($_eck_entity_types_cache) ? TRUE : FALSE;

    if ($cache_enabled) {
      $_eck_entity_types_cache->reset();
    }

    eck_clean_up_request();
  }

  /**
   * Delete.
   */
  public function delete() {
    // Delete all the bundles from this entity type.
    $bundles = EckBundle::loadByEckEntityType($this);
    foreach ($bundles as $bundle) {
      $bundle->delete();
    }

    db_drop_table('eck_' . $this->name);

    // Delete the entity cache table for this entity.
    db_drop_table("cache_entity_{$this->name}");

    // Allow other modules to respond to the deletion of entity types.
    module_invoke_all('eck_entity_type_delete', $this);
    global $_eck_entity_types_cache;

    $cache_enabled = isset($_eck_entity_types_cache) ? TRUE : FALSE;

    if ($cache_enabled) {
      $_eck_entity_types_cache->reset();
    }
    eck_clean_up_request();
  }

  /**
   * Load a specific entity type.
   *
   * @param string $name
   *   The name of the entity type.
   *
   * @return EckEntityType|NULL
   */
  public static function loadByName($name) {
    $entity_types = self::loadAll();

    if (array_key_exists($name, $entity_types)) {
      return $entity_types[$name];
    }

    return NULL;
  }

  /**
   * Load all entity types.
   *
   * @param bool $reset
   *   Use data from the cache, or not.
   *
   * @return array
   *   An array of entity types keyed by entity type name.
   */
  public static function loadAll($reset = FALSE) {
    $entity_types = array();

    global $_eck_entity_types_cache;

    $cache_enabled = isset($_eck_entity_types_cache) ? TRUE : FALSE;

    if ($cache_enabled) {
      if ($reset) {
        $_eck_entity_types_cache->reset();
      }

      $entity_types = $_eck_entity_types_cache->get();
    }

    if (empty($entity_types)) {
      $config_names = config_get_names_with_prefix('eck.entity_type.');
      foreach ($config_names as $config_name) {
        $name = str_replace('eck.entity_type.', '', $config_name);
        $entity_type = new EckEntityType();
        $entity_type->load($name);
        $entity_types[$name] = $entity_type;
      }

      if ($cache_enabled) {
        $_eck_entity_types_cache->set($entity_types);
      }
    }
    return $entity_types;
  }

  /**
   * Get the label.
   */
  public function label() {
    return $this->label;
  }
}

/**
 * A bundle database object.
 */
class EckBundle {

  /**
   * The configuration settings for the bundle.
   *
   * @var array
   */
  public $settings;

  /**
   * The name of the bundle.
   *
   * @var string
   */
  public $name;

  /**
   * The human-readable label of the bundle.
   *
   * @var string
   */
  public $label;

  /**
   * The entity type the bundle belongs to.
   *
   * @var string
   */
  public $entity_type;


  /**
   * Constructor.
   */
  public function __construct() {
    $this->settings = array();
  }

  /**
   * Load bundle.
   *
   * @param string $entity_type_name
   *   The name of the entity type.
   * @param string $bundle_name
   *   The name of the bundle.
   */
  public function load(string $entity_type_name, string $bundle_name) {
    $config = config('eck.bundle.' . $entity_type_name . '.' . $bundle_name);
    if (!empty($config) && !$config->isNew()) {
      foreach ($config->get() as $property => $value) {
        $this->{$property} = $value;
      }
    }
  }

  /**
   * @param array $machineNames
   * @return EckBundle[]
   */
  public static function loadMultipleByMachineName(array $machineNames) {
    $bundles = array();

    foreach ($machineNames as $machineName) {
      $bundle = self::loadByMachineName($machineName);
      if (!empty($bundle)) {
        $bundles[$machineName] = $bundle;
      }
    }

    return $bundles;
  }

  /**
   * Create a machine name.
   */
  private function createMachineName() {
    $this->machine_name = "{$this->entity_type}_{$this->name}";
  }

  /**
   * Create a label.
   */
  private function createLabel() {
    $name = $this->name;
    $pieces = explode("_", $name);
    $final = array();
    foreach ($pieces as $piece) {
      $final[] = ucfirst($piece);
    }

    $this->label = implode(" ", $final);
  }

  /**
   * Save the bundle.
   */
  public function save() {
    // Lets do some checks before the bundle is saved.
    if (isset($this->entity_type) && isset($this->name)) {

      $save = TRUE;

      // Lets set the machine name.
      $this->createMachineName();

      // If this bundle isNew we need to check that it does not exist.
      // @todo we just need to change the field in the db to be unique.
      if ($this->isNew) {
        $bundle = self::loadByMachineName($this->machine_name);
        if (!empty($bundle) && !$bundle->isNew) {
          $save = FALSE;
        }
      }

      if (!isset($this->label)) {
        $this->createLabel();
      }

      if ($save) {
        global $_eck_bundles_cache;

        $config = config('eck.bundle.' . $this->entity_type . '.' . $this->name);
        $config->set('entity_type', $this->entity_type);
        $config->set('name', $this->name);
        $config->set('label', $this->label);
        $config->set('settings', $this->settings);
        $config->save();

        $cache_enabled = isset($_eck_bundles_cache) ? TRUE : FALSE;

        if ($cache_enabled) {
          $_eck_bundles_cache->reset();
        }

        eck_clean_up_request();
      }
      else {
        // @todo Throw some error.
      }
    }
    else {
      // If the name an entity type are not set, we can not save the bundle.
      // @todo Throw some error or exception.
    }
  }

  /**
   * Delete the bundle.
   */
  public function delete() {
    // First delete all of the entities of this bundle.
    $query = new EntityFieldQuery();

    $query->entityCondition('entity_type', $this->entity_type, '=')
      ->entityCondition('bundle', $this->name, '=');

    $results = $query->execute();

    if (!empty($results)) {
      $ids = array_keys($results[$this->entity_type]);
      entity_delete_multiple($this->entity_type, $ids);
    }

    // Then we delete the bundle (field_instances).
    field_attach_delete_bundle($this->entity_type, $this->name);

    global $_eck_bundles_cache;

    $cache_enabled = isset($_eck_bundles_cache) ? TRUE : FALSE;

    if ($cache_enabled) {
      $_eck_bundles_cache->reset();
    }

    eck_clean_up_request();
  }

  /**
   * This method returns a bundle object.
   *
   * @param string $machine_name
   *   A string composed of the entity type name and the bundle name,
   *   e.g., "{$entity_type_name}_{$bundle_name}".
   *
   * @return EckBundle|Null
   */
  public static function loadByMachineName($machine_name) {
    $bundles = self::loadAll();

    if (array_key_exists($machine_name, $bundles)) {
      return $bundles[$machine_name];
    }

    return NULL;
  }

  /**
   * Load all bundles.
   *
   * @param bool $reset
   *   Retrive from cache if availabe or not.
   *
   * @return array
   *   An array of Bundles, keyed by the bundle "machine name" (e.g.,
   *   "{$entity_type_name}_{$bundle_name}").
   */
  public static function loadAll($reset = FALSE) {
    $bundles = array();

    global $_eck_bundles_cache;

    $cache_enabled = isset($_eck_bundles_cache) ? TRUE : FALSE;

    if ($cache_enabled) {
      if ($reset) {
        $_eck_bundles_cache->reset();
      }

      $bundles = $_eck_bundles_cache->get();
    }

    if (empty($bundles)) {
      $bundles = array();
      $config_names = config_get_names_with_prefix('eck.');
      foreach ($config_names as $config_name) {
        $name_parts = explode('.', $config_name);
        if ($name_parts[1] != 'entity_type') {
          $name = end($name_parts);
          $entity_type_name = $name_parts[2];
          $bundle = new EckBundle();
          $bundle->load($entity_type_name, $name);
          $bundles[$entity_type_name . '_' . $name] = $bundle;
        }
      }

      if ($cache_enabled) {
        $_eck_bundles_cache->set($bundles);
      }
    }

    return $bundles;
  }

  /**
   * Load the bundles of an entity type.
   *
   * @param EckEntityType $entity_type
   *   An entity type object.
   *
   * @return array
   *   An array with the bundles associated with the given entity type.
   */
  public static function loadByEckEntityType($entity_type) {
    $entity_bundles = &backdrop_static(__FUNCTION__, array());

    $entity_type_name = $entity_type->name;

    if (!isset($entity_bundles[$entity_type_name])) {
      $entity_bundles[$entity_type_name] = array();
      $bundles = self::loadAll();

      foreach ($bundles as $bundle) {
        if ($entity_type_name == $bundle->entity_type) {
          $entity_bundles[$entity_type_name][] = $bundle;
        }
      }
    }

    return (isset($entity_bundles[$entity_type_name])) ? $entity_bundles[$entity_type_name] : array();
  }

  /**
   * Adds a field to this bundle.
   *
   * See: @link field Field API data structures @endlink.
   *
   * @param string $field_type
   *   The type of field to add. One of the keys as defined by any field module
   *   using hook_field_info.
   * @param array $options
   *   This is an optional array. Its properties can include:
   *   _use existing_: If TRUE and if a 'field_name' property is specified in
   *   the 'field' property below and the field already exists, then a new
   *   instance will be created using the existing field. All specified 'field
   *   options provided other then the field name will be ignored. If FALSE,
   *   and an existing field is found then a new field_name will be generated.
   *   TRUE by default.
   *   _field_: all options accepted by field_create_field(). Defaults will be
   *   used for each property that is omitted. Most defaults come from
   *   field_create_field(). Default 'field_name'generation.
   *
   * @return bool
   *   The $instance array with the id property filled in as returned by
   *   field_create_instance().
   *
   * @throws \FieldException
   */
  public function addField($field_type, $options = array()) {
    // Check that the field type is known.
    $field_info = field_info_field_types($field_type);
    if (!$field_info) {
      throw new FieldException(t('Attempt to add a field of unknown type %type.', array('%type' => $field_type)));
    }

    // By default use an existing field if one is found.
    $options += array('use existing' => TRUE);
    // Set field options and merge in any provided field settings.
    $field = array('type' => $field_type);
    if (!empty($options['field'])) {
      $field += $options['field'];
    }

    // Retrieve existing fields of this type.
    $field_type_fields = field_read_fields(array('type' => $field_type), array('include_inactive' => TRUE));

    // Formulate a default field name.
    if (empty($field['field_name']) || (isset($field_type_fields[$field['field_name']]) && !$options['use existing'])) {
      $iter = count($field_type_fields) + 1;
      $field += array(
        'field_name' => substr('field_' . $field_type, 0, 28) . '_' . $iter,
      );
    }

    // Create a new field if the field name is unique over active and
    // disabled fields.
    if (!isset($field_type_fields[$field['field_name']])) {
      field_create_field($field);
    }

    // Add an instance of the field to this bundle.
    $instance = array(
      'field_name' => $field['field_name'],
      'entity_type' => $this->entity_type,
      'bundle' => $this->name,
    );
    // Merge any provided properties and settings.
    if (array_key_exists('instance', $options)) {
      $instance += $options['instance'];
    }
    return field_create_instance($instance);
  }

}

/**
 * A class that manages ECK caches.
 */
class EckCache {

  private $id;
  private $data;

  /**
   * Inititate the ECK cache manager.
   *
   * @param string $id
   *   What are we caching or retriving? entity_type, bundle, etc.
   */
  public function __construct($id) {
    $this->id = $id;
  }

  /**
   * Set the cache.
   */
  public function set($data) {
    cache_set($this->id, $data, "cache_eck");
    $this->data = $data;
  }

  /**
   * Get something from the cache.
   */
  public function get() {
    if ($this->data) {
      return $this->data;
    }
    else {
      $cached_data = cache_get($this->id, "cache_eck");
      if ($cached_data) {
        $this->data = $cached_data->data;
        return $this->data;
      }
      else {
        return NULL;
      }
    }
  }

  /**
   * Reset the cache.
   */
  public function reset() {
    $this->data = NULL;
    cache_clear_all($this->id, 'cache_eck');
  }

}
