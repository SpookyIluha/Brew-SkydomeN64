#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include "skydome.h"


void dir_from_euler(fm_vec3_t *out, const fm_vec3_t *euler) {
    out->z = -cos(euler->y)*cos(euler->x);
    out->x = -sin(euler->y)*cos(euler->x);
    out->y = sin(euler->x);
}

/**
 * Simple example with a 3d-model file created in blender.
 * This uses the builtin model format for loading and drawing a model.
 */
int main()
{
	debug_init_isviewer();
	debug_init_usblog();
	asset_init_compression(2);

  dfs_init(DFS_DEFAULT_LOCATION);

  display_init((resolution_t){.width = 480, .height = 320, .interlaced = true, .aspect_ratio = 16.0f / 9.0f}, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);

  rdpq_init();
  joypad_init();

  rdpq_font_t* font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
  rdpq_text_register_font(1, font);

  t3d_init((T3DInitParams){});
  T3DViewport viewport = t3d_viewport_create();

  T3DMat4 modelMat; // matrix for our model, this is a "normal" float matrix
  t3d_mat4_identity(&modelMat);

  // Now allocate a fixed-point matrix, this is what t3d uses internally.
  // Note: this gets DMA'd to the RSP, so it needs to be uncached.
  // If you can't allocate uncached memory, remember to flush the cache after writing to it instead.
  T3DMat4FP* modelMatFP = malloc_uncached(sizeof(T3DMat4FP));
  T3DMat4FP* modelMatFP2 = malloc_uncached(sizeof(T3DMat4FP));

  T3DModel* model = t3d_model_load("rom:/city.t3dm");
  T3DModel* model_terr = t3d_model_load("rom:/city_terrain.t3dm");
  rspq_block_t* modelblock = NULL;

  T3DVec3 camPos = {{0 ,155.0f,90.0f}};
  T3DVec3 camTarget;
  fm_vec3_t camrotation = {{0 ,T3D_DEG_TO_RAD(180), 0}};

  skydome_t* sky = skydome_create();
  skydome_load_data(sky, NULL,NULL,NULL); // load default models and textures
  skydome_load_data_lensflare(sky, NULL,NULL,NULL);
  skydome_time_of_day(sky, 100);
  skydome_set_viewport(sky, &viewport);
  sky->clouds.density = 1;
  sky->clouds.opacity = 1;

  sky->clouds.speed.x = -0.002f;
  sky->clouds.speed.y = -0.001f;

  sky->clouds.speedclouds.x = -0.01f;
  sky->clouds.speedclouds.y = -0.00f;

  while (1)
  {
    // ======== Update ======== //

    // time of day is a float between 0-86400 seconds with 0 being noon
    skydome_time_of_day(sky, TICKS_TO_MS(timer_ticks()) / 2); // get the time of day colors and settings based on actual time
    skydome_cloud_pass(sky, display_get_delta_time() * 1000); // move the clouds with time
    
    joypad_poll();
    
    joypad_inputs_t input = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    fm_vec3_t forward = {0}, right = {0};
    camrotation.y     -= T3D_DEG_TO_RAD(0.28f * display_get_delta_time() * input.stick_x);
    camrotation.x     += T3D_DEG_TO_RAD(0.28f * display_get_delta_time() * input.stick_y);
    fm_vec3_t eulerright = camrotation; eulerright.y += T3D_DEG_TO_RAD(90);
    dir_from_euler(&forward, &camrotation);
    dir_from_euler(&right, &eulerright);

    fm_vec3_norm(&forward, &forward);
    fm_vec3_norm(&right, &right);
    fm_vec3_scale(&forward, &forward, 15.0f * display_get_delta_time());
    fm_vec3_scale(&right, &right, 15.0f * display_get_delta_time());
    right.y = 0;
    if(held.d_left)  fm_vec3_add((fm_vec3_t*)&camPos, (fm_vec3_t*)&camPos, &right);
    if(held.d_right) fm_vec3_sub((fm_vec3_t*)&camPos, (fm_vec3_t*)&camPos, &right);
    if(held.d_up)    fm_vec3_add((fm_vec3_t*)&camPos, (fm_vec3_t*)&camPos, &forward);
    if(held.d_down)  fm_vec3_sub((fm_vec3_t*)&camPos, (fm_vec3_t*)&camPos, &forward);

    if(held.l) camPos.y += 15.0f * display_get_delta_time();
    if(held.z) camPos.y -= 15.0f * display_get_delta_time();

    fm_vec3_scale(&forward, &forward, 10);
    t3d_vec3_add(&camTarget, &camPos, (T3DVec3*)&forward);

    t3d_viewport_set_perspective(&viewport, T3D_DEG_TO_RAD(90.0f), 16.0f / 9.0f, 9.0f, 1100.0f);
    t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

    t3d_mat4_from_srt_euler(&modelMat,
      (float[3]){0.2f, 0.2f, 0.2f},
      (float[3]){0, 0, 0},
      (float[3]){camPos.x,camPos.y,camPos.z}
    );
    t3d_mat4_to_fixed(modelMatFP, &modelMat);
    t3d_mat4fp_from_srt_euler(modelMatFP2,
      (float[3]){1, 1, 1},
      (float[3]){0, 0, 0},
      (float[3]){0,0,0}
    );

    // ======== Draw ======== //
    rdpq_attach(display_get(), display_get_zbuf());
    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    t3d_screen_clear_depth();

      t3d_matrix_push(modelMatFP);
      skydome_draw(sky); // draw the main halfsphere first with no zbuf
      t3d_matrix_pop(1);

      
      rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
      rdpq_mode_zbuf(true,true);
      rdpq_mode_antialias(AA_STANDARD);

      if(!modelblock){
        rspq_block_begin();
        t3d_matrix_push(modelMatFP2);
        t3d_model_draw(model_terr);
        rdpq_mode_zbuf(true,true);
        t3d_model_draw(model);
        t3d_matrix_pop(1);
        modelblock = rspq_block_end();
      } rspq_block_run(modelblock);

      rdpq_set_mode_standard();
      rdpq_mode_filter(FILTER_BILINEAR);
      rdpq_mode_antialias(true);
      rdpq_set_env_color(RGBA32(0xFF, 0xFF, 0xFF, 0x80));
      rdpq_mode_combiner(RDPQ_COMBINER1((ENV,PRIM,ENV_ALPHA,PRIM), (PRIM,0,TEX0,0)));
      rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

      skydome_draw_lensflare(sky); // draw the lensflares last with no Zbuf checks other than the internal one
      rdpq_call_deferred((void (*)(void *))&skydome_lensflare_update_zbuf, sky); // update the lensflare's depth sample so that if its occluded, its not drawn (requires Zbuffer)
      rdpq_detach_show();

  }

  t3d_destroy();
  return 0;
}
