/*
 * Copyright (C) 2006-2013 Christopho, Solarus - http://www.solarus-games.org
 *
 * Solarus is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Solarus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "Camera.h"
#include "Map.h"
#include "entities/MapEntity.h"
#include "entities/MapEntities.h"
#include "entities/Hero.h"
#include "entities/Separator.h"
#include "movements/TargetMovement.h"
#include "lowlevel/VideoManager.h"
#include "lowlevel/System.h"
#include "lua/LuaContext.h"

/**
 * \brief Creates a camera.
 * \param map The map.
 */
Camera::Camera(Map& map):
  map(map),
  position(VideoManager::get_instance()->get_quest_size()),
  fixed_on_hero(true),
  separator_scrolling_dx(0),
  separator_scrolling_dy(0),
  separator_next_scrolling_date(0),
  restoring(false),
  speed(120),
  movement(NULL) {
}

/**
 * \brief Destructor.
 */
Camera::~Camera() {
  delete movement;
}

/**
 * \brief Returns the width of the visible area shown by the camera.
 * \return The width of the quest screen.
 */
int Camera::get_width() {
  return position.get_width();
}

/**
 * \brief Returns the height of the visible area shown by the camera.
 * \return The height of the quest screen.
 */
int Camera::get_height() {
  return position.get_height();
}

/**
 * \brief Updates the camera position.
 *
 * This function is called continuously by the game loop.
 */
void Camera::update() {

  if (fixed_on_hero) {
    // If the camera is not moving towards a target, center it on the hero.
    update_fixed_on_hero();
  }
  else if (movement != NULL) {
    update_moving();
  }
}

/**
 * \brief Updates the position of the camera when the camera is fixed
 * on the hero.
 */
void Camera::update_fixed_on_hero() {

  Debug::check_assertion(fixed_on_hero,
      "Illegal call to Camera::update_fixed_on_hero()");

  int x = 0;
  int y = 0;
  if (separator_next_scrolling_date == 0) {
    // Normal case: not traversing a separator.

    // First compute the camera coordinates ignoring map borders and separators.
    const Rectangle& hero_center = map.get_entities().get_hero().get_center_point();
    const int hero_x = hero_center.get_x();
    const int hero_y = hero_center.get_y();
    x = hero_x - get_width() / 2;
    y = hero_y - get_height() / 2;

    // See if there is a separator in the rectangle.
    // TODO simplify: treat horizontal separators first and then all vertical ones.
    int adjusted_x = x;  // Updated coordinates after applying separators.
    int adjusted_y = y;
    std::list<Separator*> applied_separators;
    const std::list<Separator*>& separators =
        map.get_entities().get_separators();
    std::list<Separator*>::const_iterator it;
    for (it = separators.begin(); it != separators.end(); ++it) {
      Separator& separator = *(*it);

      if (separator.is_vertical()) {
        // Vertical separator.
        int separation_x = separator.get_x() + 8;

        if (x < separation_x && separation_x < x + get_width()
            && separator.get_y() < y + get_height()
            && y < separator.get_y() + separator.get_height()) {
          int left = separation_x - x;
          int right = x + get_width() - separation_x;
          if (left > right) {
            adjusted_x = separation_x - get_width();
          }
          else {
            adjusted_x = separation_x;
          }
          applied_separators.push_back(&separator);
        }
      }
      else {
        Debug::check_assertion(separator.is_horizontal(), "Invalid separator shape");

        // Horizontal separator.
        int separation_y = separator.get_y() + 8;
        if (y < separation_y && separation_y < y + get_height()
            && separator.get_x() < x + get_width()
            && x < separator.get_x() + separator.get_width()) {
          int top = separation_y - y;
          int bottom = y + get_height() - separation_y;
          if (top > bottom) {
            adjusted_y = separation_y - get_height();
          }
          else {
            adjusted_y = separation_y;
          }
          applied_separators.push_back(&separator);
        }
      }
    }  // End for each separator.

    bool must_adjust_x = true;
    bool must_adjust_y = true;
    if (adjusted_x != x && adjusted_y != y) {
      // Both directions were modified. Maybe it is a T configuration where
      // a separator inactivates another one.

      must_adjust_x = false;
      must_adjust_y = false;
      std::list<Separator*>::const_iterator it;
      for (it = applied_separators.begin(); it != applied_separators.end(); ++it) {
        Separator& separator = *(*it);

        if (separator.is_vertical()) {
          // Vertical separator.
          int separation_x = separator.get_x() + 8;

          if (x < separation_x && separation_x < x + get_width()
              && separator.get_y() < adjusted_y + get_height()
              && adjusted_y < separator.get_y() + separator.get_height()) {
            must_adjust_x = true;
          }
        }
        else {
          // Horizontal separator.
          int separation_y = separator.get_y() + 8;

          if (y < separation_y && separation_y < y + get_height()
              && separator.get_x() < adjusted_x + get_width()
              && adjusted_x < separator.get_x() + separator.get_width()) {
            must_adjust_y = true;
          }
        }
      }  // End for each separator applied.
    } // End if both directions.

    if (must_adjust_x) {
      x = adjusted_x;
    }
    if (must_adjust_y) {
      y = adjusted_y;
    }
  }
  else {
    // Traversing a separator.
    uint32_t now = System::now();
    while (separator_next_scrolling_date != 0
        && now >= separator_next_scrolling_date) {
      separator_scrolling_position.add_xy(
          separator_scrolling_dx, separator_scrolling_dy);

      separator_next_scrolling_date += 1;

      if (separator_scrolling_position.equals(separator_target_position)) {
        // Finished.
        separator_next_scrolling_date = 0;
      }
    }
    x = separator_scrolling_position.get_x();
    y = separator_scrolling_position.get_y();
  }

  // Take care of the limits of the map.
  const Rectangle& map_location = map.get_location();
  if (map_location.get_width() < get_width()) {
    x = (map_location.get_width() - get_width()) / 2;
  }
  else {
    x = std::min(std::max(x, 0),
        map_location.get_width() - get_width());
  }

  if (map_location.get_height() < get_height()) {
    y = (map_location.get_height() - get_height()) / 2;
  }
  else {
    y = std::min(std::max(y, 0),
        map_location.get_height() - get_height());
  }

  position.set_xy(x, y);
}

/**
 * \brief Updates the position of the camera when the camera is moving
 * towards a point or back to the hero.
 */
void Camera::update_moving() {

  Debug::check_assertion(!fixed_on_hero,
      "Illegal call to Camera::update_fixed_on_hero()");

  if (movement == NULL) {
    return;
  }

  movement->update();
  int x = movement->get_x() - get_width() / 2;
  int y = movement->get_y() - get_height() / 2;

  if (movement->is_finished()) {
    delete movement;
    movement = NULL;

    if (restoring) {
      restoring = false;
      fixed_on_hero = true;
      map.get_lua_context().map_on_camera_back(map);
    }
    else {
      map.get_lua_context().notify_camera_reached_target(map);
    }
  }

  position.set_xy(x, y);
}

/**
 * \brief Returns the current position of the camera.
 *
 * This function returns the rectangle of the visible area of this camera.
 *
 * \return The visible area.
 */
const Rectangle& Camera::get_position() {
  return position;
}

/**
 * \brief Returns whether there is a camera movement.
 *
 * It may be a movement towards a point or a scrolling movement due to a
 * separator.
 *
 * \return \c true if the camera is moving.
 */
bool Camera::is_moving() {
  return !fixed_on_hero                      // Moving to a point.
      || separator_next_scrolling_date != 0;  // Traversing a separator.
}

/**
 * \brief Sets the speed of the camera movement.
 * \param speed speed of the movement in pixels per second
 */
void Camera::set_speed(int speed) {
  this->speed = speed;
}

/**
 * \brief Makes the camera move towards a destination point.
 *
 * The camera will be centered on this point.
 * If there was already a movement, the new one replaces it.
 *
 * \param target_x x coordinate of the target point
 * \param target_y y coordinate of the target point
 */
void Camera::move(int target_x, int target_y) {

  if (movement != NULL) {
    delete movement;
  }

  const Rectangle& map_location = map.get_location();
  target_x = std::min(std::max(target_x, get_width() / 2),
      map_location.get_width() - get_width() / 2);
  target_y = std::min(std::max(target_y, get_height() / 2),
      map_location.get_height() - get_height() / 2);

  movement = new TargetMovement(NULL, target_x, target_y, speed, true);
  movement->set_xy(position.get_x() + get_width() / 2, position.get_y() + get_height() / 2);

  fixed_on_hero = false;
}

/**
 * \brief Makes the camera move towards an entity.
 *
 * The camera will be centered on the entity's center point.
 * If there was already a movement, the new one replaces it.
 * Note that the camera will not update its movement if the entity moves.
 *
 * \param entity the target entity
 */
void Camera::move(MapEntity& entity) {

  const Rectangle& center = entity.get_center_point();
  move(center.get_x(), center.get_y());
}

/**
 * \brief Moves the camera back to the hero.
 *
 * The hero is not supposed to move during this time.
 * Once the movement is finished, the camera starts following the hero again.
 */
void Camera::restore() {

  move(map.get_entities().get_hero());
  restoring = true;
}

/**
 * \brief Starts traversing a separator.
 *
 * The hero must touch the separator when you call this function.
 *
 * \param Separator The separator to traverse.
 */
void Camera::traverse_separator(const Separator& separator) {

  // Save the current position of the camera.
  separator_scrolling_position = position;

  // Start scrolling.
  separator_scrolling_dx = 0;
  separator_scrolling_dy = 0;
  separator_target_position = position;
  Hero& hero = map.get_entities().get_hero();
  const Rectangle& hero_center = hero.get_center_point();
  const Rectangle& separator_center = separator.get_center_point();
  if (separator.is_horizontal()) {
    if (hero_center.get_y() < separator_center.get_y()) {
      separator_scrolling_dy = 1;
      separator_target_position.add_y(get_height());
    }
    else {
      separator_scrolling_dy = -1;
      separator_target_position.add_y(-get_height());
    }
  }
  else {
    if (hero_center.get_x() < separator_center.get_x()) {
      separator_scrolling_dx = 1;
      separator_target_position.add_x(get_width());
    }
    else {
      separator_scrolling_dx = -1;
      separator_target_position.add_x(-get_width());
    }
  }

  separator_next_scrolling_date = System::now();

  // Move the hero two pixels ahead to avoid to traversed the separator again.
  hero.set_xy(hero.get_x() + 2 * separator_scrolling_dx,
      hero.get_y() + 2 * separator_scrolling_dy);
}

