/*
* Copyright (C) 2024-2026 FebriCahyaa
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
 * @brief Sets all environment variables for the profiler
 */
void set_profiler_env_vars();

void run_perfcommon(void);
/// Zram, refresh range and graphics props; skipped when unchanged unless @p force.
void apply_system_tweaks(bool force = false);
void apply_performance_profile(bool lite_mode, std::string game_pkg, pid_t game_pid);
void apply_performance_lite_profile(std::string game_pkg, pid_t game_pid);
void apply_balance_profile();
void apply_powersave_profile();
