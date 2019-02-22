/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

/*
 *
 * Polygon object interpreter
 *
 */

#include <stdexcept>
#include <stdlib.h>
#include "dxxsconf.h"
#include "dsx-ns.h"
#include "dxxerror.h"

#include "interp.h"
#include "common/3d/globvars.h"
#include "polyobj.h"
#include "gr.h"
#include "byteutil.h"
#include "u_mem.h"

namespace dcx {

constexpr std::integral_constant<unsigned, 0> OP_EOF{};   //eof
constexpr std::integral_constant<unsigned, 1> OP_DEFPOINTS{};   //defpoints
constexpr std::integral_constant<unsigned, 2> OP_FLATPOLY{};   //flat-shaded polygon
constexpr std::integral_constant<unsigned, 3> OP_TMAPPOLY{};   //texture-mapped polygon
constexpr std::integral_constant<unsigned, 4> OP_SORTNORM{};   //sort by normal
constexpr std::integral_constant<unsigned, 5> OP_RODBM{};   //rod bitmap
constexpr std::integral_constant<unsigned, 6> OP_SUBCALL{};   //call a subobject
constexpr std::integral_constant<unsigned, 7> OP_DEFP_START{};   //defpoints with start
constexpr std::integral_constant<unsigned, 8> OP_GLOW{};   //glow value for next poly

#if DXX_USE_EDITOR
int g3d_interp_outline;
#endif

}

namespace dsx {

static int16_t init_model_sub(uint8_t *p, int16_t);

#if defined(DXX_BUILD_DESCENT_I) || DXX_WORDS_BIGENDIAN
static inline int16_t *wp(uint8_t *p)
{
	return reinterpret_cast<int16_t *>(p);
}
#endif

static inline const int16_t *wp(const uint8_t *p)
{
	return reinterpret_cast<const int16_t *>(p);
}

static inline const vms_vector *vp(const uint8_t *p)
{
	return reinterpret_cast<const vms_vector *>(p);
}

static inline int16_t w(const uint8_t *p)
{
	return *wp(p);
}

static void rotate_point_list(g3s_point *dest, const vms_vector *src, uint_fast32_t n)
{
	while (n--)
		g3_rotate_point(*dest++,*src++);
}

constexpr vms_angvec zero_angles = {0,0,0};

namespace {

class interpreter_ignore_op_defpoints
{
public:
	static void op_defpoints(const uint8_t *, uint16_t)
	{
	}
};

class interpreter_ignore_op_defp_start
{
public:
	static void op_defp_start(const uint8_t *, uint16_t)
	{
	}
};

class interpreter_ignore_op_flatpoly
{
public:
	static void op_flatpoly(const uint8_t *, uint16_t)
	{
	}
};

class interpreter_ignore_op_tmappoly
{
public:
	static void op_tmappoly(const uint8_t *, uint16_t)
	{
	}
};

class interpreter_ignore_op_rodbm
{
public:
	static void op_rodbm(const uint8_t *)
	{
	}
};

class interpreter_ignore_op_glow
{
public:
	static void op_glow(const uint8_t *)
	{
	}
};

class interpreter_base
{
public:
	static uint16_t get_raw_opcode(const uint8_t *const p)
	{
		return w(p);
	}
	static uint_fast32_t translate_opcode(const uint8_t *, const uint16_t op)
	{
		return op;
	}
	static uint16_t get_op_subcount(const uint8_t *const p)
	{
		return w(p + 2);
	}
	__attribute_cold
	static void op_default()
	{
		throw std::runtime_error("invalid polygon model");
	}
};

class g3_poly_get_color_state :
	public interpreter_ignore_op_defpoints,
	public interpreter_ignore_op_defp_start,
	public interpreter_ignore_op_tmappoly,
	public interpreter_ignore_op_rodbm,
	public interpreter_ignore_op_glow,
	public interpreter_base
{
public:
	int color;
	g3_poly_get_color_state() :
		color(0)
	{
	}
	void op_flatpoly(const uint8_t *const p, const uint_fast32_t nv)
	{
		if (nv > MAX_POINTS_PER_POLY)
			return;
		if (g3_check_normal_facing(*vp(p+4),*vp(p+16)) > 0) {
#if defined(DXX_BUILD_DESCENT_I)
			color = (w(p+28));
#elif defined(DXX_BUILD_DESCENT_II)
			color = gr_find_closest_color_15bpp(w(p + 28));
#endif
		}
	}
	void op_sortnorm(const uint8_t *const p)
	{
		const bool facing = g3_check_normal_facing(*vp(p+16),*vp(p+4)) > 0;
		color = g3_poly_get_color(facing ? p + w(p + 28) : p + w(p + 30));
	}
	void op_subcall(const uint8_t *const p)
	{
#if defined(DXX_BUILD_DESCENT_I)
		color = g3_poly_get_color(p+w(p+16));
#elif defined(DXX_BUILD_DESCENT_II)
		(void)p;
#endif
	}
};

class g3_interpreter_draw_base
{
protected:
	grs_bitmap *const *const model_bitmaps;
	polygon_model_points &Interp_point_list;
	grs_canvas &canvas;
	const submodel_angles anim_angles;
	const g3s_lrgb model_light;
private:
	void rotate(uint_fast32_t i, const vms_vector *const src, const uint_fast32_t n)
	{
		rotate_point_list(&Interp_point_list[i], src, n);
	}
	void set_color_by_model_light(fix g3s_lrgb::*const c, g3s_lrgb &o, const fix color) const
	{
		o.*c = fixmul(color, model_light.*c);
	}
protected:
	template <std::size_t N>
		array<cg3s_point *, N> prepare_point_list(const uint_fast32_t nv, const uint8_t *const p)
		{
			array<cg3s_point *, N> point_list;
			for (uint_fast32_t i = 0; i < nv; ++i)
				point_list[i] = &Interp_point_list[wp(p + 30)[i]];
			return point_list;
		}
	g3s_lrgb get_noglow_light(const uint8_t *const p) const
	{
		g3s_lrgb light;
		const auto negdot = -vm_vec_dot(View_matrix.fvec, *vp(p + 16));
		const auto color = (f1_0 / 4) + ((negdot * 3) / 4);
		set_color_by_model_light(&g3s_lrgb::r, light, color);
		set_color_by_model_light(&g3s_lrgb::g, light, color);
		set_color_by_model_light(&g3s_lrgb::b, light, color);
		return light;
	}
	g3_interpreter_draw_base(grs_bitmap *const *const mbitmaps, polygon_model_points &plist, grs_canvas &ccanvas, const submodel_angles aangles, const g3s_lrgb &mlight) :
		model_bitmaps(mbitmaps), Interp_point_list(plist),
		canvas(ccanvas),
		anim_angles(aangles), model_light(mlight)
	{
	}
	void op_defpoints(const vms_vector *const src, const uint_fast32_t n)
	{
		rotate(0, src, n);
	}
	void op_defp_start(const uint8_t *const p, const vms_vector *const src, const uint_fast32_t n)
	{
		rotate(static_cast<int>(w(p + 4)), src, n);
	}
	static std::pair<uint16_t, uint16_t> get_sortnorm_offsets(const uint8_t *const p)
	{
		const uint16_t a = w(p + 30), b = w(p + 28);
		return (g3_check_normal_facing(*vp(p + 16), *vp(p + 4)) > 0)
			? std::make_pair(a, b)	//draw back then front
			: std::make_pair(b, a)	//not facing.  draw front then back
		;
	}
	void op_rodbm(const uint8_t *const p)
	{
		const auto &&rod_bot_p = g3_rotate_point(*vp(p + 20));
		const auto &&rod_top_p = g3_rotate_point(*vp(p + 4));
		const g3s_lrgb rodbm_light{
			f1_0, f1_0, f1_0
		};
		g3_draw_rod_tmap(canvas, *model_bitmaps[w(p + 2)], rod_bot_p, w(p + 16), rod_top_p, w(p + 32), rodbm_light);
	}
	void op_subcall(const uint8_t *const p, const glow_values_t *const glow_values)
	{
		g3_start_instance_angles(*vp(p + 4), anim_angles ? anim_angles[w(p + 2)] : zero_angles);
		g3_draw_polygon_model(model_bitmaps, Interp_point_list, canvas, anim_angles, model_light, glow_values, p + w(p + 16));
		g3_done_instance();
	}
};

class g3_draw_polygon_model_state :
	public interpreter_base,
	g3_interpreter_draw_base
{
	const glow_values_t *const glow_values;
	unsigned glow_num;
public:
	g3_draw_polygon_model_state(grs_bitmap *const *const mbitmaps, polygon_model_points &plist, grs_canvas &ccanvas, const submodel_angles aangles, const g3s_lrgb &mlight, const glow_values_t *const glvalues) :
		g3_interpreter_draw_base(mbitmaps, plist, ccanvas, aangles, mlight),
		glow_values(glvalues),
		glow_num(~0u)		//glow off by default
	{
	}
	void op_defpoints(const uint8_t *const p, const uint_fast32_t n)
	{
		g3_interpreter_draw_base::op_defpoints(vp(p + 4), n);
	}
	void op_defp_start(const uint8_t *const p, const uint_fast32_t n)
	{
		g3_interpreter_draw_base::op_defp_start(p, vp(p + 8), n);
	}
	void op_flatpoly(const uint8_t *const p, const uint_fast32_t nv)
	{
		if (nv > MAX_POINTS_PER_POLY)
			return;
		if (g3_check_normal_facing(*vp(p+4),*vp(p+16)) > 0)
		{
#if defined(DXX_BUILD_DESCENT_II)
			if (!glow_values || !(glow_num < glow_values->size()) || (*glow_values)[glow_num] != -3)
#endif
			{
				//					DPH: Now we treat this color as 15bpp
#if defined(DXX_BUILD_DESCENT_I)
				const uint8_t color = w(p + 28);
#elif defined(DXX_BUILD_DESCENT_II)
				const uint8_t color = (glow_values && glow_num < glow_values->size() && (*glow_values)[glow_num] == -2)
					? 255
					: gr_find_closest_color_15bpp(w(p + 28));
#endif
				const auto point_list = prepare_point_list<MAX_POINTS_PER_POLY>(nv, p);
				g3_draw_poly(*grd_curcanv, nv, point_list, color);
			}
		}
	}
	static g3s_lrgb get_glow_light(const fix c)
	{
		return {c, c, c};
	}
	void op_tmappoly(const uint8_t *const p, const uint_fast32_t nv)
	{
		if (nv > MAX_POINTS_PER_POLY)
			return;
		if (!(g3_check_normal_facing(*vp(p+4),*vp(p+16)) > 0))
			return;
		//calculate light from surface normal
		const auto &&light = (glow_values && glow_num < glow_values->size())
			? get_glow_light((*glow_values)[exchange(glow_num, -1)]) //yes glow
			: get_noglow_light(p); //no glow
		//now poke light into l values
		array<g3s_uvl, MAX_POINTS_PER_POLY> uvl_list;
		array<g3s_lrgb, MAX_POINTS_PER_POLY> lrgb_list;
		const fix average_light = (light.r + light.g + light.b) / 3;
		for (uint_fast32_t i = 0; i != nv; i++)
		{
			lrgb_list[i] = light;
			uvl_list[i] = (reinterpret_cast<const g3s_uvl *>(p+30+((nv&~1)+1)*2))[i];
			uvl_list[i].l = average_light;
		}
		const auto point_list = prepare_point_list<MAX_POINTS_PER_POLY>(nv, p);
		g3_draw_tmap(canvas, nv, point_list, uvl_list, lrgb_list, *model_bitmaps[w(p + 28)]);
	}
	void op_sortnorm(const uint8_t *const p)
	{
		const auto &&offsets = get_sortnorm_offsets(p);
		const auto a = offsets.first;
		const auto b = offsets.second;
		g3_draw_polygon_model(model_bitmaps, Interp_point_list, canvas, anim_angles, model_light, glow_values, p + a);
		g3_draw_polygon_model(model_bitmaps, Interp_point_list, canvas, anim_angles, model_light, glow_values, p + b);
	}
	using g3_interpreter_draw_base::op_rodbm;
	void op_subcall(const uint8_t *const p)
	{
		g3_interpreter_draw_base::op_subcall(p, glow_values);
	}
	void op_glow(const uint8_t *const p)
	{
		glow_num = w(p+2);
	}
};

class g3_draw_morphing_model_state :
	public interpreter_ignore_op_glow,
	public interpreter_base,
	g3_interpreter_draw_base
{
	const vms_vector *const new_points;
	static constexpr const glow_values_t *glow_values = nullptr;
public:
	g3_draw_morphing_model_state(grs_bitmap *const *const mbitmaps, polygon_model_points &plist, grs_canvas &ccanvas, const submodel_angles aangles, const g3s_lrgb &mlight, const vms_vector *const npoints) :
		g3_interpreter_draw_base(mbitmaps, plist, ccanvas, aangles, mlight),
		new_points(npoints)
	{
	}
	void op_defpoints(const uint8_t *, const uint_fast32_t n)
	{
		g3_interpreter_draw_base::op_defpoints(new_points, n);
	}
	void op_defp_start(const uint8_t *const p, const uint_fast32_t n)
	{
		g3_interpreter_draw_base::op_defp_start(p, new_points, n);
	}
	void op_flatpoly(const uint8_t *const p, const uint_fast32_t nv)
	{
		int ntris;
		const uint8_t color = w(p+28);
		unsigned i;
		auto point_list = prepare_point_list<3>(i = 2, p);
		for (ntris=nv-2;ntris;ntris--) {
			point_list[2] = &Interp_point_list[wp(p+30)[i++]];
			g3_check_and_draw_poly(canvas, point_list, color);
			point_list[1] = point_list[2];
		}
	}
	void op_tmappoly(const uint8_t *const p, const uint_fast32_t nv)
	{
		if (nv > MAX_POINTS_PER_POLY)
			return;
		array<g3s_uvl, MAX_POINTS_PER_POLY> uvl_list;
		array<g3s_lrgb, MAX_POINTS_PER_POLY> lrgb_list;
		lrgb_list.fill(get_noglow_light(p));
		for (uint_fast32_t i = 0; i != nv; i++)
			uvl_list[i] = (reinterpret_cast<const g3s_uvl *>(p+30+((nv&~1)+1)*2))[i];
		const auto point_list = prepare_point_list<MAX_POINTS_PER_POLY>(nv, p);
		g3_draw_tmap(canvas, nv, point_list, uvl_list, lrgb_list, *model_bitmaps[w(p + 28)]);
	}
	void op_sortnorm(const uint8_t *const p)
	{
		const auto &&offsets = get_sortnorm_offsets(p);
		auto &a = offsets.first;
		auto &b = offsets.second;
		g3_draw_morphing_model(canvas, p + a, model_bitmaps, anim_angles, model_light, new_points, Interp_point_list);
		g3_draw_morphing_model(canvas, p + b, model_bitmaps, anim_angles, model_light, new_points, Interp_point_list);
	}
	using g3_interpreter_draw_base::op_rodbm;
	void op_subcall(const uint8_t *const p)
	{
		g3_interpreter_draw_base::op_subcall(p, glow_values);
	}
};

class init_model_sub_state :
	public interpreter_ignore_op_defpoints,
	public interpreter_ignore_op_defp_start,
	public interpreter_ignore_op_rodbm,
	public interpreter_ignore_op_glow,
	public interpreter_base
{
public:
	int16_t highest_texture_num;
	init_model_sub_state(int16_t h) :
		highest_texture_num(h)
	{
	}
	void op_flatpoly(uint8_t *const p, const uint_fast32_t nv)
	{
		(void)nv;
		Assert(nv > 2);		//must have 3 or more points
#if defined(DXX_BUILD_DESCENT_I)
		*wp(p+28) = static_cast<short>(gr_find_closest_color_15bpp(w(p+28)));
#elif defined(DXX_BUILD_DESCENT_II)
		(void)p;
#endif
	}
	void op_tmappoly(const uint8_t *const p, const uint_fast32_t nv)
	{
		(void)nv;
		Assert(nv > 2);		//must have 3 or more points
		if (w(p+28) > highest_texture_num)
			highest_texture_num = w(p+28);
	}
	void op_sortnorm(uint8_t *const p)
	{
		auto h = init_model_sub(p+w(p+28), highest_texture_num);
		highest_texture_num = init_model_sub(p+w(p+30), h);
	}
	void op_subcall(uint8_t *const p)
	{
		highest_texture_num = init_model_sub(p+w(p+16), highest_texture_num);
	}
};

constexpr const glow_values_t *g3_draw_morphing_model_state::glow_values;

}

template <typename P, typename State>
static std::size_t dispatch_polymodel_op(const P p, State &state, const uint_fast32_t op)
{
	switch (op)
	{
		case OP_DEFPOINTS: {
			const auto n = state.get_op_subcount(p);
			const std::size_t record_size = n * sizeof(vms_vector) + 4;
			state.op_defpoints(p, n);
			return record_size;
		}
		case OP_DEFP_START: {
			const auto n = state.get_op_subcount(p);
			const std::size_t record_size = n * sizeof(vms_vector) + 8;
			state.op_defp_start(p, n);
			return record_size;
		}
		case OP_FLATPOLY: {
			const auto n = state.get_op_subcount(p);
			const std::size_t record_size = 30 + ((n & ~1) + 1) * 2;
			state.op_flatpoly(p, n);
			return record_size;
		}
		case OP_TMAPPOLY: {
			const auto n = state.get_op_subcount(p);
			const std::size_t record_size = 30 + ((n & ~1) + 1) * 2 + n * 12;
			state.op_tmappoly(p, n);
			return record_size;
		}
		case OP_SORTNORM: {
			const std::size_t record_size = 32;
			state.op_sortnorm(p);
			return record_size;
		}
		case OP_RODBM: {
			const std::size_t record_size = 36;
			state.op_rodbm(p);
			return record_size;
		}
		case OP_SUBCALL: {
			const std::size_t record_size = 20;
			state.op_subcall(p);
			return record_size;
		}
		case OP_GLOW: {
			const std::size_t record_size = 4;
			state.op_glow(p);
			return record_size;
		}
		default:
			state.op_default();
			return 2;
	}
}

template <typename P, typename State>
static P iterate_polymodel(P p, State &state)
{
	for (uint16_t op; (op = state.get_raw_opcode(p)) != OP_EOF;)
		p += dispatch_polymodel_op(p, state, state.translate_opcode(p, op));
	return p;
}

}

#if DXX_WORDS_BIGENDIAN
namespace dcx {

static inline fix *fp(uint8_t *p)
{
	return reinterpret_cast<fix *>(p);
}

static inline vms_vector *vp(uint8_t *p)
{
	return reinterpret_cast<vms_vector *>(p);
}

static void short_swap(short *s)
{
	*s = SWAPSHORT(*s);
}

static void fix_swap(fix &f)
{
	f = SWAPINT(f);
}

static void fix_swap(fix *f)
{
	fix_swap(*f);
}

static void vms_vector_swap(vms_vector &v)
{
	fix_swap(v.x);
	fix_swap(v.y);
	fix_swap(v.z);
}

namespace {

class swap_polygon_model_data_state : public interpreter_base
{
public:
	static uint_fast32_t translate_opcode(uint8_t *const p, const uint16_t op)
	{
		return *wp(p) = INTEL_SHORT(op);
	}
	static uint16_t get_op_subcount(const uint8_t *const p)
	{
		return SWAPSHORT(w(p + 2));
	}
	static void op_defpoints(uint8_t *const p, const uint_fast32_t n)
	{
		*wp(p + 2) = n;
		for (uint_fast32_t i = 0; i != n; ++i)
			vms_vector_swap(*vp((p + 4) + (i * sizeof(vms_vector))));
	}
	static void op_defp_start(uint8_t *const p, const uint_fast32_t n)
	{
		*wp(p + 2) = n;
		short_swap(wp(p + 4));
		for (uint_fast32_t i = 0; i != n; ++i)
			vms_vector_swap(*vp((p + 8) + (i * sizeof(vms_vector))));
	}
	static void op_flatpoly(uint8_t *const p, const uint_fast32_t n)
	{
		*wp(p + 2) = n;
		vms_vector_swap(*vp(p + 4));
		vms_vector_swap(*vp(p + 16));
		short_swap(wp(p+28));
		for (uint_fast32_t i = 0; i < n; ++i)
			short_swap(wp(p + 30 + (i * 2)));
	}
	static void op_tmappoly(uint8_t *const p, const uint_fast32_t n)
	{
		*wp(p + 2) = n;
		vms_vector_swap(*vp(p + 4));
		vms_vector_swap(*vp(p + 16));
		for (uint_fast32_t i = 0; i != n; ++i) {
			const auto uvl_val = reinterpret_cast<g3s_uvl *>((p+30+((n&~1)+1)*2) + (i * sizeof(g3s_uvl)));
			fix_swap(&uvl_val->u);
			fix_swap(&uvl_val->v);
		}
		short_swap(wp(p+28));
		for (uint_fast32_t i = 0; i != n; ++i)
			short_swap(wp(p + 30 + (i * 2)));
	}
	void op_sortnorm(uint8_t *const p)
	{
		vms_vector_swap(*vp(p + 4));
		vms_vector_swap(*vp(p + 16));
		short_swap(wp(p + 28));
		short_swap(wp(p + 30));
		swap_polygon_model_data(p + w(p+28));
		swap_polygon_model_data(p + w(p+30));
	}
	static void op_rodbm(uint8_t *const p)
	{
		vms_vector_swap(*vp(p + 20));
		vms_vector_swap(*vp(p + 4));
		short_swap(wp(p+2));
		fix_swap(fp(p + 16));
		fix_swap(fp(p + 32));
	}
	void op_subcall(uint8_t *const p)
	{
		short_swap(wp(p+2));
		vms_vector_swap(*vp(p+4));
		short_swap(wp(p+16));
		swap_polygon_model_data(p + w(p+16));
	}
	static void op_glow(uint8_t *const p)
	{
		short_swap(wp(p + 2));
	}
};

}

void swap_polygon_model_data(ubyte *data)
{
	swap_polygon_model_data_state state;
	iterate_polymodel(data, state);
}

}
#endif

#if DXX_WORDS_NEED_ALIGNMENT
namespace dcx {

static void add_chunk(const uint8_t *old_base, uint8_t *new_base, int offset,
	       chunk *chunk_list, int *no_chunks)
{
	Assert(*no_chunks + 1 < MAX_CHUNKS); //increase MAX_CHUNKS if you get this
	chunk_list[*no_chunks].old_base = old_base;
	chunk_list[*no_chunks].new_base = new_base;
	chunk_list[*no_chunks].offset = offset;
	chunk_list[*no_chunks].correction = 0;
	(*no_chunks)++;
}

namespace {

class get_chunks_state :
	public interpreter_ignore_op_defpoints,
	public interpreter_ignore_op_defp_start,
	public interpreter_ignore_op_flatpoly,
	public interpreter_ignore_op_tmappoly,
	public interpreter_ignore_op_rodbm,
	public interpreter_ignore_op_glow,
	public interpreter_base
{
	const uint8_t *const data;
	uint8_t *const new_data;
	chunk *const list;
	int *const no;
public:
	get_chunks_state(const uint8_t *const p, uint8_t *const ndata, chunk *const l, int *const n) :
		data(p), new_data(ndata), list(l), no(n)
	{
	}
	static uint_fast32_t translate_opcode(const uint8_t *, const uint16_t op)
	{
		return INTEL_SHORT(op);
	}
	static uint16_t get_op_subcount(const uint8_t *const p)
	{
		return GET_INTEL_SHORT(p + 2);
	}
	void op_sortnorm(const uint8_t *const p)
	{
		add_chunk(p, p - data + new_data, 28, list, no);
		add_chunk(p, p - data + new_data, 30, list, no);
	}
	void op_subcall(const uint8_t *const p)
	{
		add_chunk(p, p - data + new_data, 16, list, no);
	}
};

}

/*
 * finds what chunks the data points to, adds them to the chunk_list, 
 * and returns the length of the current chunk
 */
int get_chunks(const uint8_t *data, uint8_t *new_data, chunk *list, int *no)
{
	get_chunks_state state(data, new_data, list, no);
	auto p = iterate_polymodel(data, state);
	return p + 2 - data;
}

}
#endif //def WORDS_NEED_ALIGNMENT

namespace dsx {

// check a polymodel for it's color and return it
int g3_poly_get_color(const uint8_t *p)
{
	g3_poly_get_color_state state;
	iterate_polymodel(p, state);
	return state.color;
}

//calls the object interpreter to render an object.  The object renderer
//is really a seperate pipeline. returns true if drew
void g3_draw_polygon_model(grs_bitmap *const *const model_bitmaps, polygon_model_points &Interp_point_list, grs_canvas &canvas, const submodel_angles anim_angles, const g3s_lrgb model_light, const glow_values_t *const glow_values, const uint8_t *const p)
{
	g3_draw_polygon_model_state state(model_bitmaps, Interp_point_list, canvas, anim_angles, model_light, glow_values);
	iterate_polymodel(p, state);
}

#ifndef NDEBUG
static int nest_count;
#endif

//alternate interpreter for morphing object
void g3_draw_morphing_model(grs_canvas &canvas, const uint8_t *const p, grs_bitmap *const *const model_bitmaps, const submodel_angles anim_angles, const g3s_lrgb model_light, const vms_vector *new_points, polygon_model_points &Interp_point_list)
{
	g3_draw_morphing_model_state state(model_bitmaps, Interp_point_list, canvas, anim_angles, model_light, new_points);
	iterate_polymodel(p, state);
}

static int16_t init_model_sub(uint8_t *p, int16_t highest_texture_num)
{
	init_model_sub_state state(highest_texture_num);
	Assert(++nest_count < 1000);
	iterate_polymodel(p, state);
	return state.highest_texture_num;
}

//init code for bitmap models
int16_t g3_init_polygon_model(void *model_ptr)
{
	#ifndef NDEBUG
	nest_count = 0;
	#endif

	return init_model_sub(reinterpret_cast<uint8_t *>(model_ptr), -1);
}

}
