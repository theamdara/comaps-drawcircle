#include "drape_frontend/drape_api_builder.hpp"
#include "drape_frontend/batcher_bucket.hpp"
#include "drape_frontend/colored_symbol_shape.hpp"
#include "drape_frontend/gui/gui_text.hpp"
#include "drape_frontend/line_shape.hpp"
#include "drape_frontend/map_shape.hpp"
#include "drape_frontend/render_state_extension.hpp"
#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/tile_key.hpp"

#include "drape/attribute_provider.hpp"
#include "drape/batcher.hpp"
#include "drape/drape_global.hpp"
#include "drape/glsl_types.hpp"
#include "drape/utils/vertex_decl.hpp"

#include "shaders/programs.hpp"

#include "indexer/feature_decl.hpp"

#include "geometry/circle_on_earth.hpp"
#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/spline.hpp"

#include "coding/point_coding.hpp"

#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"

namespace
{
void BuildText(ref_ptr<dp::GraphicsContext> context, std::string const & str, dp::FontDecl const & font,
               m2::PointD const & position, m2::PointD const & center, ref_ptr<dp::TextureManager> textures,
               dp::Batcher & batcher)
{
  gui::StaticLabel::LabelResult result;
  gui::StaticLabel::CacheStaticText(str, "\n", dp::LeftTop, font, textures, result);
  glsl::vec2 const pt = glsl::ToVec2(df::MapShape::ConvertToLocal(position, center, df::kShapeCoordScalar));
  for (gui::StaticLabel::Vertex & v : result.m_buffer)
    v.m_position = glsl::vec3(pt, 0.0f);

  dp::AttributeProvider provider(1 /* streamCount */, static_cast<uint32_t>(result.m_buffer.size()));
  provider.InitStream(0 /* streamIndex */, gui::StaticLabel::Vertex::GetBindingInfo(),
                      make_ref(result.m_buffer.data()));

  batcher.InsertListOfStrip(context, result.m_state, make_ref(&provider), dp::Batcher::VertexPerQuad);
}
}  // namespace

namespace df
{
void DrapeApiBuilder::BuildLines(ref_ptr<dp::GraphicsContext> context, DrapeApi::TLines & lines,
                                 ref_ptr<dp::TextureManager> textures,
                                 std::vector<drape_ptr<DrapeApiRenderProperty>> & properties)
{
  properties.reserve(lines.size());

  uint32_t constexpr kMaxSize = 5000;
  uint32_t constexpr kFontSize = 14;
  FeatureID fakeFeature;

  for (auto & line : lines)
  {
    std::string const & id = line.first;

    DrapeApiLineData & data = line.second;
    base::Unique(data.m_points,
                 [](m2::PointD const & p1, m2::PointD const & p2) { return p1.EqualDxDy(p2, kMwmPointAccuracy); });

    m2::RectD rect;
    for (auto const & p : data.m_points)
      rect.Add(p);

    dp::Batcher batcher(kMaxSize, kMaxSize);
    batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
    auto property = make_unique_dp<DrapeApiRenderProperty>();
    property->m_center = rect.Center();
    {
      dp::SessionGuard guard(context, batcher,
                             [&property, &id](dp::RenderState const & state, drape_ptr<dp::RenderBucket> && b)
      {
        property->m_id = id;
        property->m_buckets.emplace_back(state, std::move(b));
      });

      if (data.m_points.size() > 1)
      {
        m2::SharedSpline spline(data.m_points);
        LineViewParams lvp;
        lvp.m_tileCenter = property->m_center;
        lvp.m_depthTestEnabled = false;
        lvp.m_minVisibleScale = 1;
        lvp.m_cap = dp::RoundCap;
        lvp.m_color = data.m_color;
        lvp.m_width = data.m_width;
        lvp.m_join = dp::RoundJoin;
        LineShape(spline, lvp).Draw(context, make_ref(&batcher), textures);
      }

      if (data.m_showPoints)
      {
        ColoredSymbolViewParams cvp;
        cvp.m_tileCenter = property->m_center;
        cvp.m_depthTestEnabled = false;
        cvp.m_minVisibleScale = 1;
        cvp.m_shape = ColoredSymbolViewParams::Shape::Circle;
        cvp.m_color = data.m_color;
        cvp.m_radiusInPixels = data.m_width * 2.0f;
        for (m2::PointD const & pt : data.m_points)
        {
          ColoredSymbolShape(m2::PointD(pt), cvp, TileKey(), 0 /* textIndex */, false /* need overlay */)
              .Draw(context, make_ref(&batcher), textures);
        }
      }

      if (data.m_markPoints || data.m_showId)
      {
        dp::FontDecl font(data.m_color, kFontSize);
        size_t index = 0;
        for (m2::PointD const & pt : data.m_points)
        {
          if (index > 0 && !data.m_markPoints)
            break;

          std::string s;
          if (data.m_markPoints)
            s = strings::to_string(index) + ((data.m_showId && index == 0) ? (" (" + id + ")") : "");
          else
            s = id;

          BuildText(context, s, font, pt, property->m_center, textures, batcher);
          index++;
        }
      }
    }

    if (!property->m_buckets.empty())
      properties.push_back(std::move(property));
  }
}

void DrapeApiBuilder::BuildCircles(ref_ptr<dp::GraphicsContext> context, DrapeApi::TCircles & circles,
                                   ref_ptr<dp::TextureManager> textures,
                                   std::vector<drape_ptr<DrapeApiRenderProperty>> & properties)
{
  properties.reserve(properties.size() + circles.size());

  uint32_t constexpr kMaxSize = 5000;
  double constexpr kMinRadiusM = 0.1;

  for (auto & circle : circles)
  {
    std::string const & id = circle.first;
    DrapeApiCircleData & data = circle.second;
    if (data.m_radiusM < kMinRadiusM)
      continue;

    auto ring = ms::CreateCircleGeometryOnEarth(mercator::ToLatLon(data.m_center), data.m_radiusM,
                                                data.m_angleStepDeg);
    if (ring.size() < 3)
      continue;

    // Keep the ring near the center when it crosses the antimeridian,
    // otherwise the triangle fan would stretch across the whole map.
    for (auto & pt : ring)
    {
      while (pt.x - data.m_center.x > 180.0)
        pt.x -= 360.0;
      while (pt.x - data.m_center.x < -180.0)
        pt.x += 360.0;
    }

    dp::Batcher batcher(kMaxSize, kMaxSize);
    batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
    auto property = make_unique_dp<DrapeApiRenderProperty>();
    property->m_center = data.m_center;
    {
      dp::SessionGuard guard(context, batcher,
                             [&property, &id](dp::RenderState const & state, drape_ptr<dp::RenderBucket> && b)
      {
        property->m_id = id;
        property->m_buckets.emplace_back(state, std::move(b));
      });

      dp::TextureManager::ColorRegion region;
      textures->GetColorRegion(data.m_color, region);
      glsl::vec2 const uv = glsl::ToVec2(region.GetTexRect().Center());

      auto const program = data.m_color.GetAlpha() == 255 ? gpu::Program::Area : gpu::Program::TransparentArea;
      auto state = CreateRenderState(program, DepthLayer::OverlayLayer);
      state.SetDepthTestEnabled(false);
      state.SetColorTexture(region.GetTexture());

      auto const toLocal = [&data](m2::PointD const & pt)
      { return glsl::ToVec2(MapShape::ConvertToLocal(pt, data.m_center, kShapeCoordScalar)); };

      glsl::vec2 const centerLocal = toLocal(data.m_center);
      std::vector<gpu::AreaVertex> vertices;
      vertices.reserve(ring.size() * 3);
      for (size_t i = 0; i < ring.size(); ++i)
      {
        vertices.emplace_back(glsl::vec3(centerLocal, 0.0f), uv);
        vertices.emplace_back(glsl::vec3(toLocal(ring[(i + 1) % ring.size()]), 0.0f), uv);
        vertices.emplace_back(glsl::vec3(toLocal(ring[i]), 0.0f), uv);
      }

      dp::AttributeProvider provider(1, static_cast<uint32_t>(vertices.size()));
      provider.InitStream(0, gpu::AreaVertex::GetBindingInfo(), make_ref(vertices.data()));
      batcher.InsertTriangleList(context, state, make_ref(&provider));

      // A thin solid outline keeps the boundary visible at low fill opacity.
      auto outline = ring;
      outline.push_back(ring.front());
      LineViewParams lvp;
      lvp.m_tileCenter = data.m_center;
      lvp.m_depthTestEnabled = false;
      lvp.m_minVisibleScale = 1;
      lvp.m_color = dp::Color(data.m_color.GetRed(), data.m_color.GetGreen(), data.m_color.GetBlue(), 255);
      lvp.m_width = 2.0f;
      LineShape(m2::SharedSpline(outline), lvp).Draw(context, make_ref(&batcher), textures);
    }

    if (!property->m_buckets.empty())
      properties.push_back(std::move(property));
  }
}
}  // namespace df
