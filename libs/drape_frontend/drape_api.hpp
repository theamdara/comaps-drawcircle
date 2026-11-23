#pragma once

#include "drape_frontend/drape_engine_safe_ptr.hpp"

#include "drape/color.hpp"
#include "drape/pointers.hpp"

#include "geometry/point2d.hpp"

#include <mutex>
#include <string>
#include <vector>

#include "3party/ankerl/unordered_dense.h"

namespace df
{
class DrapeEngine;

/**
 * @brief Geometry and style for a line rendered with Drape.
 */
struct DrapeApiLineData
{
  DrapeApiLineData() = default;

  DrapeApiLineData(std::vector<m2::PointD> const & points, dp::Color const & color) : m_points(points), m_color(color)
  {}

  /**
   * @brief Enables showing the points of the line.
   *
   * Calling this method will set the internal showPoints attribute to true (causing the points of
   * the line to be shown as dots) and the markPoints attribute to the value supplied.
   *
   * @param markPoints Whether to mark the points.
   */
  DrapeApiLineData & ShowPoints(bool markPoints)
  {
    m_showPoints = true;
    m_markPoints = markPoints;
    return *this;
  }

  /**
   * @brief Sets the width for the line in pixels.
   */
  DrapeApiLineData & Width(float width)
  {
    m_width = width;
    return *this;
  }

  DrapeApiLineData & ShowId()
  {
    m_showId = true;
    return *this;
  }

  std::vector<m2::PointD> m_points;
  float m_width = 1.0f;
  dp::Color m_color;

  bool m_showPoints = false;
  bool m_markPoints = false;
  bool m_showId = false;
};

/**
 * @brief Geometry and style for a filled circle rendered with Drape.
 *
 * The circle is defined by a center in mercator coordinates and a radius in
 * meters on Earth. |m_angleStepDeg| controls how finely the ring is
 * approximated (smaller values produce smoother circles at the cost of more
 * geometry).
 */
struct DrapeApiCircleData
{
  DrapeApiCircleData() = default;
  DrapeApiCircleData(m2::PointD const & center, double radiusM, dp::Color const & color)
    : m_center(center), m_radiusM(radiusM), m_color(color)
  {}

  m2::PointD m_center;
  double m_radiusM = 0.0;
  dp::Color m_color;
  double m_angleStepDeg = 5.0;
};

class DrapeApi
{
public:
  using TLines = ankerl::unordered_dense::map<std::string, DrapeApiLineData>;
  using TCircles = ankerl::unordered_dense::map<std::string, DrapeApiCircleData>;

  DrapeApi() = default;

  void SetDrapeEngine(ref_ptr<DrapeEngine> engine);

  /**
   * @brief Adds a new line.
   *
   * @param id A unique identifier for the line. If a line with the same identifier exists, it will
   * be replaced by the new line.
   * @param data The data (geometry and style) for the new line.
   */
  void AddLine(std::string const & id, DrapeApiLineData const & data);

  /**
   * @brief Removes a line.
   *
   * @param id The id of the line to remove.
   */
  void RemoveLine(std::string const & id);

  /**
   * @brief Adds a new filled circle.
   *
   * @param id A unique identifier for the circle. If a circle with the same id exists, it will be
   * replaced by the new one. Use ids that do not collide with line ids.
   * @param data The geometry and style of the circle.
   */
  void AddCircle(std::string const & id, DrapeApiCircleData const & data);

  /**
   * @brief Removes a circle.
   *
   * @param id The id of the circle to remove.
   */
  void RemoveCircle(std::string const & id);

  /**
   * @brief Removes all circles added with AddCircle.
   */
  void ClearCircles();

  /**
   * @brief Removes all lines and circles added with this API.
   */
  void Clear();
  void Invalidate();

private:
  DrapeEngineSafePtr m_engine;
  TLines m_lines;
  TCircles m_circles;
};
}  // namespace df
