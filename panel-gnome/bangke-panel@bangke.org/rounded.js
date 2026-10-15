import GObject from 'gi://GObject';
import Shell from 'gi://Shell';

// Clip the composed backdrop, including blur, rather than only its CSS fill.
// Applying this outside the blur actor prevents rectangular glass in the corners.
export const RoundedClip = GObject.registerClass(class BangkeRoundedClip extends Shell.GLSLEffect {
  vfunc_build_pipeline() {
    this.add_glsl_snippet(Shell.SnippetHook.FRAGMENT,
      'uniform vec2 panel_size; uniform float panel_radius;', `
      vec2 half_size = panel_size * 0.5;
      float radius = min(panel_radius, min(half_size.x, half_size.y));
      vec2 point = abs(cogl_tex_coord_in[0].xy * panel_size - half_size);
      vec2 delta = point - half_size + vec2(radius);
      float distance = length(max(delta, vec2(0.0))) + min(max(delta.x, delta.y), 0.0) - radius;
      cogl_color_out *= 1.0 - smoothstep(-0.5, 0.5, distance);
      `, false);
  }
  update(width, height, radius) {
    this.set_uniform_float(this.get_uniform_location('panel_size'), 2, [Math.max(1, width), Math.max(1, height)]);
    this.set_uniform_float(this.get_uniform_location('panel_radius'), 1, [radius]);
  }
});

// A distinct effect type keeps its pipeline uniforms separate from the outer mask.
export const BackgroundCutout = GObject.registerClass(class BangkeBackgroundCutout extends Shell.GLSLEffect {
  vfunc_build_pipeline() {
    this.add_glsl_snippet(Shell.SnippetHook.FRAGMENT,
      'uniform vec2 fill_size; uniform vec4 cut_rect; uniform float cut_radius; uniform float cut_enabled;', `
      vec2 cut_half = cut_rect.zw * 0.5;
      vec2 cut_point = abs(cogl_tex_coord_in[0].xy * fill_size - cut_rect.xy - cut_half);
      vec2 cut_delta = cut_point - cut_half + vec2(cut_radius);
      float cut_distance = length(max(cut_delta, vec2(0.0))) + min(max(cut_delta.x, cut_delta.y), 0.0) - cut_radius;
      cogl_color_out *= mix(1.0, smoothstep(-0.5, 0.5, cut_distance), cut_enabled);
      `, false);
  }
  cut(rect = null, radius = 0) {
    this.set_uniform_float(this.get_uniform_location('cut_rect'), 4, rect ?? [0, 0, 0, 0]);
    this.set_uniform_float(this.get_uniform_location('cut_radius'), 1, [radius]);
    this.set_uniform_float(this.get_uniform_location('cut_enabled'), 1, [rect ? 1 : 0]);
  }
  update(width, height) {
    this.set_uniform_float(this.get_uniform_location('fill_size'), 2, [Math.max(1, width), Math.max(1, height)]);
  }
});
