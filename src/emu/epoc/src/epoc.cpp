/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <common/configure.h>
#include <epoc/epoc.h>
#include <epoc/kernel/process.h>

#include <common/algorithm.h>
#include <common/chunkyseri.h>
#include <common/cvt.h>
#include <common/fileutils.h>
#include <common/log.h>
#include <common/path.h>
#include <common/random.h>

#include <disasm/disasm.h>

#include <epoc/loader/e32img.h>
#include <manager/rpkg.h>

#include <epoc/hal.h>
#include <epoc/utils/panic.h>

#ifdef ENABLE_SCRIPTING
#include <manager/script_manager.h>
#endif

#include <yaml-cpp/yaml.h>

#include <atomic>
#include <fstream>
#include <string>

#include <disasm/disasm.h>
#include <drivers/itc.h>
#include <gdbstub/gdbstub.h>

#include <epoc/kernel.h>
#include <epoc/mem.h>
#include <epoc/ptr.h>

#include <epoc/kernel/libmanager.h>
#include <epoc/loader/rom.h>
#include <epoc/timing.h>
#include <epoc/vfs.h>

#include <arm/arm_factory.h>
#include <manager/manager.h>
#include <manager/device_manager.h>

namespace eka2l1 {
    /*! A system instance, where all the magic happens. 
     *
     * Represents the Symbian system. You can switch the system version dynamiclly.
    */
    class system_impl {
        //! Global lock mutex for system.
        std::mutex mut;

        //! The library manager.
        hle::lib_manager hlelibmngr;

        //! The cpu
        arm::jitter cpu;
        arm_emulator_type jit_type;

        graphics_driver_client_ptr gdriver_client;

        memory_system mem;
        kernel_system kern;
        timing_system timing;
        manager_system mngr;

        //! The IO system
        io_system io;

        //! Disassmebly helper.
        disasm asmdis;

        gdbstub gdb_stub;

        debugger_ptr debugger;

        //! The ROM
        /*! This is the information parsed
         * from the ROM, used as utility.
        */
        loader::rom romf;

        bool reschedule_pending;

        epocver ver = epocver::epoc94;
        bool exit = false;

        std::unordered_map<std::string, bool> bool_configs;
        std::unordered_map<uint32_t, hal_ptr> hals;

        bool startup_inited = false;

        std::optional<filesystem_id> rom_fs_id = std::nullopt;

        bool save_snapshot_processes(const std::string &path,
            const std::vector<uint32_t> &inclue_uids);

        system *parent;

    public:
        system_impl(system *parent, debugger_ptr debugger, drivers::driver_instance graphics_driver,
            arm_emulator_type jit_type = arm_emulator_type::unicorn);

        ~system_impl() = default;

        void set_graphics_driver(drivers::driver_instance graphics_driver);

        void set_debugger(debugger_ptr new_debugger) {
            debugger = std::move(new_debugger);
        }

        void set_symbian_version_use(const epocver ever) {
            kern.set_epoc_version(ever);
            io.set_epoc_ver(ever);
        }

        bool set_device(const std::uint8_t idx) {
            manager::device_manager *dmngr = mngr.get_device_manager();
            bool result = dmngr->set_current(idx);

            if (!result) {
                return false;
            }

            manager::device *dvc = dmngr->get_current();
            io.set_product_code(dvc->firmware_code);
            
            set_symbian_version_use(dvc->ver);
            return true;
        }

        void set_jit_type(const arm_emulator_type type) {
            jit_type = type;
        }

        loader::rom *get_rom_info() {
            return &romf;
        }

        epocver get_symbian_version_use() const {
            return kern.get_epoc_version();
        }

        void prepare_reschedule() {
            cpu->prepare_rescheduling();
            reschedule_pending = true;
        }

        void init();
        bool load(uint32_t id);
        int loop();
        void shutdown();

        /*!\brief Snapshot is a way to save the state of the system.
         *
         * Snapshot can be used for fast startup. Here, in EKA2L1,
         * after the first UI process runs well, the state of all
         * processes will be saved and load in the next running
         * session.
         *
         * The snapshot will save all of the following:
         * - The EPOC version
         * - All kernel objects (semaphore, mutex, etc...)
         * - Global memory data that is committed.
         * - Local data for each process
         * - Thread state, current running thread and process
         *
         * The following will not be saved:
         * - The ROM content.
         * - Page that is marked as free.
         *
         * \params name The path to save the snapshot. Note that the snapshot
         *              can be really large.
         *
         * \returns     True if successfully save the snapshot
         */
        bool save_snapshot(const std::string &name);
        bool save_snapshot_exclude_current_process(const std::string &name);

        bool load_snapshot(const std::string &name);

        manager_system *get_manager_system() {
            return &mngr;
        }

        memory_system *get_memory_system() {
            return &mem;
        }

        kernel_system *get_kernel_system() {
            return &kern;
        }

        hle::lib_manager *get_lib_manager() {
            return &hlelibmngr;
        }

        io_system *get_io_system() {
            return &io;
        }

        timing_system *get_timing_system() {
            return &timing;
        }

        disasm *get_disasm() {
            return &asmdis;
        }

        gdbstub *get_gdb_stub() {
            return &gdb_stub;
        }

        graphics_driver_client_ptr get_graphic_driver_client() {
            return gdriver_client;
        }

        arm::jitter &get_cpu() {
            return cpu;
        }

        void mount(drive_number drv, const drive_media media, std::string path,
            const io_attrib attrib = io_attrib::none);

        void reset();

        /*! \brief Install an Z drive repackage. 
         *
         * \returns True on success.
         */
        bool install_rpkg(const std::string &devices_rom_path, const std::string &path);
        void load_scripts();

        void do_state(common::chunkyseri &seri);

        /*! \brief Install a SIS/SISX. */
        bool install_package(std::u16string path, drive_number drv);
        bool load_rom(const std::string &path);

        void request_exit();
        bool should_exit() const {
            return exit;
        }

        void add_new_hal(uint32_t hal_cagetory, hal_ptr hal_com);
        hal_ptr get_hal(uint32_t cagetory);
    };

    void system_impl::load_scripts() {
#ifdef ENABLE_SCRIPTING
        common::dir_iterator scripts_dir("scripts");
        scripts_dir.detail = true;

        common::dir_entry scripts_entry;

        while (scripts_dir.next_entry(scripts_entry) == 0) {
            if ((scripts_entry.type == common::FILE_REGULAR) && path_extension(scripts_entry.name) == ".py") {
                auto module_name = replace_extension(filename(scripts_entry.name), "");
                mngr.get_script_manager()->import_module("scripts/" + module_name);
            }
        }
#endif
    }

    void system_impl::do_state(common::chunkyseri &seri) {
        // Save timing first
        timing.do_state(seri);
    }

    void system_impl::init() {
        exit = false;

        // Initialize manager. It doesn't depend much on other
        mngr.init(parent, &io);
        mngr.get_config_manager()->deserialize("coreconfig.yml");

        // Initialize all the system that doesn't depend on others first
        timing.init();
        io.init();
        asmdis.init();

        file_system_inst physical_fs = 
            create_physical_filesystem(get_symbian_version_use(), "");
        
        io.add_filesystem(physical_fs);

        file_system_inst rom_fs = create_rom_filesystem(nullptr, &mem,
            get_symbian_version_use(), "");

        rom_fs_id = io.add_filesystem(rom_fs);

        cpu = arm::create_jitter(&kern, &timing, &mngr, &mem, &asmdis, &hlelibmngr, &gdb_stub, debugger, jit_type);

        mem.init(cpu, get_symbian_version_use() <= epocver::epoc6 ? ram_code_addr_eka1 : ram_code_addr,
            get_symbian_version_use() <= epocver::epoc6 ? shared_data_eka1 : shared_data,
            get_symbian_version_use() <= epocver::epoc6 ? shared_data_end_eka1 - shared_data_eka1 : ram_code_addr - shared_data);

        kern.init(parent, &timing, &mngr, &mem, &io, &hlelibmngr, cpu.get());

        epoc::init_hal(parent);
        epoc::init_panic_descriptions();

#if ENABLE_SCRIPTING == 1
        load_scripts();
#endif
    }

    system_impl::system_impl(system *parent, debugger_ptr debugger, drivers::driver_instance graphics_driver,
        arm_emulator_type jit_type)
        : jit_type(jit_type)
        , parent(parent) {
        gdriver_client = std::make_shared<drivers::graphics_driver_client>(graphics_driver);
    }

    void system_impl::set_graphics_driver(drivers::driver_instance graphics_driver) {
        gdriver_client = std::make_shared<drivers::graphics_driver_client>(graphics_driver);
    }

    bool system_impl::load(uint32_t id) {
        hlelibmngr.reset();
        hlelibmngr.init(parent, &kern, &io, &mem, get_symbian_version_use());

        if (!startup_inited) {
            for (auto &startup_app : mngr.get_config_manager()->get_values("startup")) {
                process_ptr st_up = kern.spawn_new_process(
                    common::utf8_to_ucs2(startup_app));

                if (!st_up) {
                    return false;
                }

                st_up->run();
            }

            startup_inited = true;
        }

        process_ptr pr = kern.spawn_new_process(id);
        if (!pr) {
            return false;
        }

        pr->run();
        return true;
    }

    int system_impl::loop() {
        bool should_step = false;

        if (gdb_stub.is_server_enabled()) {
            gdb_stub.handle_packet();

            if (gdb_stub.get_cpu_halt_flag()) {
                if (gdb_stub.get_cpu_step_flag()) {
                    should_step = true;
                } else {
                    return 1;
                }
            }
        }

        if (kern.crr_thread() == nullptr) {
            timing.idle();
            timing.advance();
            prepare_reschedule();
        } else {
            timing.advance();

            if (!should_step) {
                cpu->run();
            } else {
                cpu->step();
            }
        }

        if (!kern.should_terminate()) {
            kern.processing_requests();

#ifdef ENABLE_SCRIPTING
            mngr.get_script_manager()->call_reschedules();
#endif

            kern.reschedule();

            reschedule_pending = false;
        } else {
            kern.crr_process().reset();

            exit = true;
            return 0;
        }

        return 1;
    }

    bool system_impl::install_package(std::u16string path, drive_number drv) {
        return mngr.get_package_manager()->install_package(path, drv);
    }

    bool system_impl::load_rom(const std::string &path) {
        std::optional<loader::rom> romf_res = loader::load_rom(path);

        if (!romf_res) {
            return false;
        }

        romf = std::move(*romf_res);

        if (rom_fs_id) {
            io.remove_filesystem(*rom_fs_id);
        }

        file_system_inst rom_fs = create_rom_filesystem(&romf, &mem,
            get_symbian_version_use(), mngr.get_device_manager()->get_current()->firmware_code);

        rom_fs_id = io.add_filesystem(rom_fs);
        bool res1 = mem.map_rom(romf.header.rom_base, path);

        if (!res1) {
            return false;
        }

        return true;
    }

    void system_impl::shutdown() {
        timing.shutdown();
        kern.shutdown();
        hlelibmngr.shutdown();
        mem.shutdown();
        asmdis.shutdown();

        // Save configs
        mngr.get_config_manager()->serialize("coreconfig.yml");

        exit = false;
    }

    void system_impl::mount(drive_number drv, const drive_media media, std::string path,
        const io_attrib attrib) {
        io.mount_physical_path(drv, media, attrib, common::utf8_to_ucs2(path));
    }

    void system_impl::request_exit() {
        cpu->stop();
        exit = true;
    }

    void system_impl::reset() {
        exit = false;
        hlelibmngr.reset();
    }

    bool system_impl::install_rpkg(const std::string &devices_rom_path, const std::string &path) {
        // TODO: Progress bar
        std::atomic_int holder;
        bool res = eka2l1::loader::install_rpkg(mngr.get_device_manager(), path, devices_rom_path,
            holder);

        if (!res) {
            return false;
        }

        return true;
    }

    void system_impl::add_new_hal(uint32_t hal_cagetory, hal_ptr hal_com) {
        hals.emplace(hal_cagetory, std::move(hal_com));
    }

    hal_ptr system_impl::get_hal(uint32_t cagetory) {
        return hals[cagetory];
    }

    bool system_impl::save_snapshot_processes(const std::string &path,
        const std::vector<uint32_t> &include_uids) {
        return true;
    }

    system::system(debugger_ptr debugger, drivers::driver_instance graphics_driver,
        arm_emulator_type jit_type)
        : impl(std::make_shared<system_impl>(this, debugger, graphics_driver, jit_type)) {
    }

    void system::set_graphics_driver(drivers::driver_instance graphics_driver) {
        return impl->set_graphics_driver(graphics_driver);
    }

    void system::set_debugger(debugger_ptr new_debugger) {
        return impl->set_debugger(new_debugger);
    }

    bool system::set_device(const std::uint8_t idx) {
        return impl->set_device(idx);
    }
    
    void system::set_symbian_version_use(const epocver ever) {
        return impl->set_symbian_version_use(ever);
    }

    void system::set_jit_type(const arm_emulator_type type) {
        return impl->set_jit_type(type);
    }

    loader::rom *system::get_rom_info() {
        return impl->get_rom_info();
    }

    epocver system::get_symbian_version_use() const {
        return impl->get_symbian_version_use();
    }

    void system::prepare_reschedule() {
        return impl->prepare_reschedule();
    }

    void system::init() {
        return impl->init();
    }

    bool system::load(uint32_t id) {
        return impl->load(id);
    }

    int system::loop() {
        return impl->loop();
    }

    void system::shutdown() {
        return impl->shutdown();
    }

    /*
    bool system::save_snapshot(const std::string &name) {
        return impl->save_snapshot(name);
    }

    bool system::save_snapshot_exclude_current_process(const std::string &name) {
        return impl->save_snapshot_exclude_current_process(name);
    }

    bool system::load_snapshot(const std::string &name) {
        return impl->load_snapshot(name);
    }
    */

    manager_system *system::get_manager_system() {
        return impl->get_manager_system();
    }
    memory_system *system::get_memory_system() {
        return impl->get_memory_system();
    }

    kernel_system *system::get_kernel_system() {
        return impl->get_kernel_system();
    }

    hle::lib_manager *system::get_lib_manager() {
        return impl->get_lib_manager();
    }

    io_system *system::get_io_system() {
        return impl->get_io_system();
    }

    timing_system *system::get_timing_system() {
        return impl->get_timing_system();
    }

    disasm *system::get_disasm() {
        return impl->get_disasm();
    }

    gdbstub *system::get_gdb_stub() {
        return impl->get_gdb_stub();
    }

    graphics_driver_client_ptr system::get_graphic_driver_client() {
        return impl->get_graphic_driver_client();
    }

    arm::jitter &system::get_cpu() {
        return impl->get_cpu();
    }

    void system::mount(drive_number drv, const drive_media media, std::string path,
        const io_attrib attrib) {
        return impl->mount(drv, media, path, attrib);
    }

    void system::reset() {
        return impl->reset();
    }

    bool system::install_rpkg(const std::string &devices_rom_path, const std::string &path) {
        return impl->install_rpkg(devices_rom_path, path);
    }

    void system::load_scripts() {
        return impl->load_scripts();
    }

    /*! \brief Install a SIS/SISX. */
    bool system::install_package(std::u16string path, drive_number drv) {
        return impl->install_package(path, drv);
    }

    bool system::load_rom(const std::string &path) {
        return impl->load_rom(path);
    }

    void system::request_exit() {
        return impl->request_exit();
    }

    bool system::should_exit() const {
        return impl->should_exit();
    }

    void system::add_new_hal(uint32_t hal_category, hal_ptr hal_com) {
        return impl->add_new_hal(hal_category, hal_com);
    }

    hal_ptr system::get_hal(uint32_t category) {
        return impl->get_hal(category);
    }

    void system::do_state(common::chunkyseri &seri) {
        return impl->do_state(seri);
    }
}