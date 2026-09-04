// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT

export interface GspSimModule {
  HEAPU8: Uint8Array;
  HEAPU16: Uint16Array;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _gsp_sim_abi_version(): number;
  _gsp_sim_capabilities(): number;
  _gsp_sim_runtime_version(): number;
  _gsp_sim_init(bundle: number, bundleSize: number): number;
  _gsp_sim_init_with_font(
    bundle: number,
    bundleSize: number,
    font: number,
    fontSize: number,
  ): number;
  _gsp_sim_destroy(): void;
  _gsp_sim_step(deltaMs: number): number;
  _gsp_sim_feed_pointer(x: number, y: number, pressed: number): void;
  _gsp_sim_feed_touch(
    id: number,
    x: number,
    y: number,
    pressed: number,
  ): void;
  _gsp_sim_set_value(bindId: number, value: number): number;
  _gsp_sim_set_color(bindId: number, rgb888: number): number;
  _gsp_sim_set_visible(bindId: number, visible: number): number;
  _gsp_sim_set_text(bindId: number, textPointer: number): number;
  _gsp_sim_keyboard_attach(actionId: number, textBind: number): number;
  _gsp_sim_set_component_i32(componentKey: number, propertyKey: number,
                             value: number): number;
  _gsp_sim_fling_messages(velocityPxPerSecond: number): number;
  _gsp_sim_goto_scene(sceneId: number): number;
  _gsp_sim_current_scene(): number;
  _gsp_sim_scene_count(): number;
  _gsp_sim_width(): number;
  _gsp_sim_height(): number;
  _gsp_sim_pixels(): number;
  _gsp_sim_stride(): number;
  _gsp_sim_pixel_format(): number;
}

export type GspSimModuleFactory = (
  options?: Record<string, unknown>,
) => Promise<GspSimModule>;

declare const createGspSimModule: GspSimModuleFactory;
export default createGspSimModule;
