cmake_minimum_required(VERSION 3.5)

project(qrustyquake)

add_executable(qrustyquake
	chase.c cl_demo.c cl_input.c cl_main.c cl_parse.c
	cl_tent.c cmd.c common.c console.c crc.c
	cvar.c d_edge.c d_init.c d_modech.c
	d_part.c d_polyse.c d_scan.c d_sky.c d_sprite.c
	d_surf.c d_vars.c draw.c host.c
	host_cmd.c in_sdl.c keys.c mathlib.c menu.c
	model.c net_bsd.c net_loop.c net_main.c
	pr_cmds.c pr_edict.c
	pr_exec.c r_aclip.c r_alias.c r_bsp.c r_draw.c
	r_edge.c r_efrag.c r_light.c r_main.c r_misc.c
	r_part.c r_sky.c r_sprite.c r_surf.c
	sbar.c screen.c snd_dma.c snd_mem.c snd_mix.c
	snd_sdl.c sv_main.c sv_move.c sv_phys.c sv_user.c
	sys_sdl.c vid_sdl.c view.c wad.c world.c
	zone.c
)

include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)

target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})

if(PSP)
	create_pbp_file(
		TARGET qrustyquake
		ICON_PATH NULL
		BACKGROUND_PATH NULL
		PREVIEW_PATH NULL
		TITLE qrustyquake
	)
endif()
