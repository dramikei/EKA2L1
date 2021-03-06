/*
 * Copyright (c) 2018 EKA2L1 Team
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

#include <epoc/services/fs/fs.h>
#include <epoc/services/fs/op.h>

#include <epoc/utils/des.h>

#include <clocale>
#include <cwctype>
#include <memory>

#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/log.h>
#include <common/path.h>
#include <common/random.h>

#include <common/e32inc.h>

#include <epoc/epoc.h>
#include <epoc/kernel.h>
#include <epoc/vfs.h>

const TUint KEntryAttNormal = 0x0000;
const TUint KEntryAttReadOnly = 0x0001;
const TUint KEntryAttHidden = 0x0002;
const TUint KEntryAttSystem = 0x0004;
const TUint KEntryAttVolume = 0x0008;
const TUint KEntryAttDir = 0x0010;
const TUint KEntryAttArchive = 0x0020;
const TUint KEntryAttXIP = 0x0080;
const TUint KEntryAttRemote = 0x0100;
const TUint KEntryAttMaskFileSystemSpecific = 0x00FF0000;
const TUint KEntryAttMatchMask = (KEntryAttHidden | KEntryAttSystem | KEntryAttDir);

const TUint KDriveAttTransaction = 0x80;
const TUint KDriveAttPageable = 0x100;
const TUint KDriveAttLogicallyRemovable = 0x200;
const TUint KDriveAttHidden = 0x400;
const TUint KDriveAttExternal = 0x800;
const TUint KDriveAttAll = 0x100000;
const TUint KDriveAttExclude = 0x40000;
const TUint KDriveAttExclusive = 0x80000;

const TUint KDriveAttLocal = 0x01;
const TUint KDriveAttRom = 0x02;
const TUint KDriveAttRedirected = 0x04;
const TUint KDriveAttSubsted = 0x08;
const TUint KDriveAttInternal = 0x10;
const TUint KDriveAttRemovable = 0x20;

const TUint KMediaAttWriteProtected = 0x08;

namespace eka2l1::epoc {
    enum TFileMode {
        EFileShareExclusive,
        EFileShareReadersOnly,
        EFileShareAny,
        EFileShareReadersOrWriters,
        EFileStream = 0,
        EFileStreamText = 0x100,
        EFileRead = 0,
        EFileWrite = 0x200,
        EFileReadAsyncAll = 0x400,
        EFileWriteBuffered = 0x00000800,
        EFileWriteDirectIO = 0x00001000,
        EFileReadBuffered = 0x00002000,
        EFileReadDirectIO = 0x00004000,
        EFileReadAheadOn = 0x00008000,
        EFileReadAheadOff = 0x00010000,
        EDeleteOnClose = 0x00020000,
        EFileBigFile = 0x00040000,
        EFileSequential = 0x00080000
    };
}

namespace eka2l1 {
    fs_handle_table::fs_handle_table() {
        for (size_t i = 0; i < nodes.size(); i++) {
            nodes[i].id = static_cast<std::uint32_t>(i + 1);
        }
    }

    size_t fs_handle_table::add_node(fs_node &node) {
        for (size_t i = 0; i < nodes.size(); i++) {
            if (!nodes[i].is_active) {
                nodes[i] = std::move(node);
                nodes[i].is_active = true;

                return i + 1;
            }
        }

        return 0;
    }

    bool fs_handle_table::close_nodes(size_t handle) {
        if (handle <= nodes.size() && nodes[handle - 1].is_active) {
            nodes[handle - 1].is_active = false;

            return true;
        }

        return false;
    }

    fs_node *fs_handle_table::get_node(size_t handle) {
        if (handle <= nodes.size() && nodes[handle - 1].is_active) {
            return &nodes[handle - 1];
        }

        return nullptr;
    }

    fs_node *fs_handle_table::get_node(const std::u16string &path) {
        for (auto &file_node : nodes) {
            if ((file_node.is_active && file_node.vfs_node->type == io_component_type::file) && (std::reinterpret_pointer_cast<file>(file_node.vfs_node)->file_name() == path)) {
                return &file_node;
            }
        }

        return nullptr;
    }

    size_t fs_path_case_insensitive_hasher::operator()(const utf16_str &key) const {
        utf16_str copy(key);

        std::locale loc("");
        for (auto &wc : copy) {
            wc = std::tolower(wc, loc);
        }

        return std::hash<utf16_str>()(copy);
    }

    bool fs_path_case_insensitive_comparer::operator()(const utf16_str &x, const utf16_str &y) const {
        return (common::compare_ignore_case(x, y) == -1);
    }

    fs_server::fs_server(system *sys)
        : service::server(sys, "!FileServer", true) {
        REGISTER_IPC(fs_server, entry, EFsEntry, "Fs::Entry");
        REGISTER_IPC(fs_server, file_open, EFsFileOpen, "Fs::FileOpen");
        REGISTER_IPC(fs_server, file_size, EFsFileSize, "Fs::FileSize");
        REGISTER_IPC(fs_server, file_set_size, EFsFileSetSize, "Fs::FileSetSize");
        REGISTER_IPC(fs_server, file_seek, EFsFileSeek, "Fs::FileSeek");
        REGISTER_IPC(fs_server, file_read, EFsFileRead, "Fs::FileRead");
        REGISTER_IPC(fs_server, file_write, EFsFileWrite, "Fs::FileWrite");
        REGISTER_IPC(fs_server, file_flush, EFsFileFlush, "Fs::FileFlush");
        REGISTER_IPC(fs_server, file_temp, EFsFileTemp, "Fs::FileTemp");
        REGISTER_IPC(fs_server, file_duplicate, EFsFileDuplicate, "Fs::FileDuplicate");
        REGISTER_IPC(fs_server, file_adopt, EFsFileAdopt, "Fs::FileAdopt");
        REGISTER_IPC(fs_server, file_rename, EFsFileRename, "Fs::FileRename(Move)");
        REGISTER_IPC(fs_server, file_replace, EFsFileReplace, "Fs::FileReplace");
        REGISTER_IPC(fs_server, file_create, EFsFileCreate, "Fs::FileCreate");
        REGISTER_IPC(fs_server, file_close, EFsFileSubClose, "Fs::FileSubClose");
        REGISTER_IPC(fs_server, file_drive, EFsFileDrive, "Fs::FileDrive");
        REGISTER_IPC(fs_server, file_name, EFsFileName, "Fs::FileName");
        REGISTER_IPC(fs_server, file_full_name, EFsFileFullName, "Fs::FileFullName");
        REGISTER_IPC(fs_server, is_file_in_rom, EFsIsFileInRom, "Fs::IsFileInRom");
        REGISTER_IPC(fs_server, open_dir, EFsDirOpen, "Fs::OpenDir");
        REGISTER_IPC(fs_server, close_dir, EFsDirSubClose, "Fs::CloseDir");
        REGISTER_IPC(fs_server, read_dir, EFsDirReadOne, "Fs::ReadDir");
        REGISTER_IPC(fs_server, read_dir_packed, EFsDirReadPacked, "Fs::ReadDirPacked");
        REGISTER_IPC(fs_server, drive_list, EFsDriveList, "Fs::DriveList");
        REGISTER_IPC(fs_server, drive, EFsDrive, "Fs::Drive");
        REGISTER_IPC(fs_server, session_path, EFsSessionPath, "Fs::SessionPath");
        REGISTER_IPC(fs_server, set_session_path, EFsSetSessionPath, "Fs::SetSessionPath");
        REGISTER_IPC(fs_server, set_session_to_private, EFsSessionToPrivate, "Fs::SetSessionToPrivate");
        REGISTER_IPC(fs_server, synchronize_driver, EFsSynchroniseDriveThread, "Fs::SyncDriveThread");
        REGISTER_IPC(fs_server, notify_change_ex, EFsNotifyChangeEx, "Fs::NotifyChangeEx");
        REGISTER_IPC(fs_server, notify_change, EFsNotifyChange, "Fs::NotifyChange");
        REGISTER_IPC(fs_server, private_path, EFsPrivatePath, "Fs::PrivatePath");
        REGISTER_IPC(fs_server, mkdir, EFsMkDir, "Fs::MkDir");
        REGISTER_IPC(fs_server, delete_entry, EFsDelete, "Fs::Delete");
        REGISTER_IPC(fs_server, rename, EFsRename, "Fs::Rename(Move)");
        REGISTER_IPC(fs_server, replace, EFsReplace, "Fs::Replace");
        REGISTER_IPC(fs_server, volume, EFsVolume, "Fs::Volume");
        REGISTER_IPC(fs_server, query_drive_info_ext, EFsQueryVolumeInfoExt, "Fs::QueryVolumeInfoExt");
        REGISTER_IPC(fs_server, set_should_notify_failure, EFsSetNotifyUser, "Fs::SetShouldNotifyFailure");
    }

    void fs_server::replace(service::ipc_context ctx) {
        auto given_path_target = ctx.get_arg<std::u16string>(0);
        auto given_path_dest = ctx.get_arg<std::u16string>(1);

        if (!given_path_target || !given_path_dest) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::u16string ss_path = session_paths[ctx.msg->msg_session->unique_id()];

        auto target = eka2l1::absolute_path(*given_path_target, ss_path);
        auto dest = eka2l1::absolute_path(*given_path_dest, ss_path);

        io_system *io = ctx.sys->get_io_system();

        // If exists, delete it so the new file can be replaced
        if (io->exist(dest)) {
            io->delete_entry(dest);
        }

        bool res = io->rename(target, dest);

        if (!res) {
            ctx.set_request_status(KErrGeneral);
            return;
        }

        // A new app list may be created
        ctx.set_request_status(KErrNone);
    }

    void fs_server::rename(service::ipc_context ctx) {
        auto given_path_target = ctx.get_arg<std::u16string>(0);
        auto given_path_dest = ctx.get_arg<std::u16string>(1);

        if (!given_path_target || !given_path_dest) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::u16string ss_path = session_paths[ctx.msg->msg_session->unique_id()];

        std::u16string target = eka2l1::absolute_path(*given_path_target, ss_path);
        std::u16string dest = eka2l1::absolute_path(*given_path_dest, ss_path);

        io_system *io = ctx.sys->get_io_system();

        if (io->exist(dest)) {
            ctx.set_request_status(KErrAlreadyExists);
            return;
        }

        bool res = io->rename(target, dest);

        if (!res) {
            ctx.set_request_status(KErrGeneral);
            return;
        }

        // A new app list may be created
        ctx.set_request_status(KErrNone);
    }

    void fs_server::delete_entry(service::ipc_context ctx) {
        auto given_path = ctx.get_arg<std::u16string>(0);

        if (!given_path) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::u16string ss_path = session_paths[ctx.msg->msg_session->unique_id()];

        auto path = eka2l1::absolute_path(*given_path, ss_path);
        io_system *io = ctx.sys->get_io_system();

        bool success = io->delete_entry(path);

        if (!success) {
            ctx.set_request_status(KErrNotFound);
            return;
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::synchronize_driver(service::ipc_context ctx) {
        ctx.set_request_status(KErrNone);
    }

    fs_node *fs_server::get_file_node(int handle) {
        return nodes_table.get_node(handle);
    }

    void fs_server::connect(service::ipc_context ctx) {
        // Please don't remove the seperator, absolute path needs this to determine root directory
        session_paths[ctx.msg->msg_session->unique_id()] = eka2l1::root_name(ctx.msg->own_thr->owning_process()->get_exe_path(), true) + u'\\';

        server::connect(ctx);
    }

    void fs_server::session_path(service::ipc_context ctx) {
        ctx.write_arg(0, session_paths[ctx.msg->msg_session->unique_id()]);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::set_session_path(service::ipc_context ctx) {
        auto new_path = ctx.get_arg<std::u16string>(0);

        if (!new_path) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        session_paths[ctx.msg->msg_session->unique_id()] = *new_path;
        ctx.set_request_status(KErrNone);
    }

    void fs_server::set_session_to_private(service::ipc_context ctx) {
        auto drive_ordinal = ctx.get_arg<int>(0);

        if (!drive_ordinal) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        char16_t drive_dos_char = char16_t(0x41 + *drive_ordinal);
        std::u16string drive_u16 = std::u16string(&drive_dos_char, 1) + u":";

        // Try to get the app uid
        uint32_t uid = std::get<2>(ctx.msg->own_thr->owning_process()->get_uid_type());
        std::string hex_id = common::to_string(uid, std::hex);

        session_paths[ctx.msg->msg_session->unique_id()] = drive_u16 + u"\\Private\\" + common::utf8_to_ucs2(hex_id) + u"\\";

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_size(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        // On Symbian^3 onwards, 64-bit file were supported, 64-bit integer for filesize used by default
        if (ctx.sys->get_kernel_system()->get_epoc_version() >= epocver::epoc10) {
            ctx.write_arg_pkg<uint64_t>(0, std::reinterpret_pointer_cast<file>(node->vfs_node)->size());
        } else {
            ctx.write_arg_pkg<uint32_t>(0,
                static_cast<std::uint32_t>(std::reinterpret_pointer_cast<file>(node->vfs_node)->size()));
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_set_size(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        if (!(node->open_mode & WRITE_MODE) && !(node->open_mode & APPEND_MODE)) {
            // Can't set file size if the file is not open for write
            ctx.set_request_status(KErrPermissionDenied);
            return;
        }

        int size = *ctx.get_arg<int>(0);
        symfile f = std::reinterpret_pointer_cast<file>(node->vfs_node);
        std::size_t fsize = f->size();

        if (size == fsize) {
            ctx.set_request_status(KErrNone);
            return;
        }

        // This is trying to prevent from data corruption that will affect the host
        if (size >= common::GB(1)) {
            LOG_ERROR("File trying to resize to 1GB, operation not permitted");
            ctx.set_request_status(KErrTooBig);

            return;
        }

        bool res = f->resize(size);

        if (!res) {
            ctx.set_request_status(KErrGeneral);
            return;
        }

        // If the file is truncated, move the file pointer to the maximum new size
        if (size < fsize) {
            f->seek(size, file_seek_mode::beg);
        }

        ctx.set_request_status(KErrNone);
    }

    struct TDriveInfo {
        TMediaType iType;
        TBatteryState iBattery;
        TUint iDriveAtt;
        TUint iMediaAtt;
        TConnectionBusType iConnectionBusType;
    };

    void fill_drive_info(TDriveInfo *info, eka2l1::drive &io_drive);

    void fs_server::file_drive(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile f = std::reinterpret_pointer_cast<file>(node->vfs_node);

        TDriveNumber drv = static_cast<TDriveNumber>(std::towlower(f->file_name()[0]) - 'a');
        TDriveInfo info;

        std::optional<eka2l1::drive> io_drive = ctx.sys->get_io_system()->get_drive_entry(
            static_cast<drive_number>(drv));

        if (!io_drive) {
            info.iType = EMediaUnknown;
        } else {
            fill_drive_info(&(info), *io_drive);
        }

        ctx.write_arg_pkg<TDriveNumber>(0, drv);
        ctx.write_arg_pkg<TDriveInfo>(1, info);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_name(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile f = std::reinterpret_pointer_cast<file>(node->vfs_node);

        ctx.write_arg(0, eka2l1::filename(f->file_name(), true));
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_full_name(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile f = std::reinterpret_pointer_cast<file>(node->vfs_node);

        ctx.write_arg(0, f->file_name());
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_seek(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::reinterpret_pointer_cast<file>(node->vfs_node);

        std::optional<int> seek_mode = ctx.get_arg<int>(1);
        std::optional<int> seek_off = ctx.get_arg<int>(0);

        if (!seek_mode || !seek_off) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        file_seek_mode vfs_seek_mode;

        switch (*seek_mode) {
        case 0: // ESeekAddress. Handle this as a normal seek start
            vfs_seek_mode = file_seek_mode::address;
            break;

        default:
            vfs_seek_mode = static_cast<file_seek_mode>(*seek_mode - 1);
            break;
        }

        // This should also support negative
        std::uint64_t seek_res = vfs_file->seek(*seek_off, vfs_seek_mode);

        if (seek_res == 0xFFFFFFFFFFFFFFFF) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        // Slot order: (0) seek offset, (1) seek mode, (2) new pos

        if ((int)ctx.sys->get_symbian_version_use() >= (int)epocver::epoc10) {
            ctx.write_arg_pkg(2, seek_res);
        } else {
            ctx.write_arg_pkg(2, static_cast<TInt>(seek_res));
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_flush(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::reinterpret_pointer_cast<file>(node->vfs_node);

        if (!vfs_file->flush()) {
            ctx.set_request_status(KErrGeneral);
            return;
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_rename(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::reinterpret_pointer_cast<file>(node->vfs_node);
        auto new_path = ctx.get_arg<std::u16string>(0);

        if (!new_path) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::u16string ss_path = session_paths[ctx.msg->msg_session->unique_id()];

        auto new_path_abs = eka2l1::absolute_path(*new_path, ss_path);
        bool res = ctx.sys->get_io_system()->rename(vfs_file->file_name(), new_path_abs);

        if (!res) {
            ctx.set_request_status(KErrGeneral);
            return;
        }

        // Save state of file and reopening it
        size_t last_pos = vfs_file->tell();
        int last_mode = vfs_file->file_mode();

        vfs_file->close();

        vfs_file = ctx.sys->get_io_system()->open_file(new_path_abs, last_mode);
        vfs_file->seek(last_pos, file_seek_mode::beg);

        node->vfs_node = std::move(vfs_file);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_write(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::optional<std::string> write_data = ctx.get_arg<std::string>(0);

        if (!write_data) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::reinterpret_pointer_cast<file>(node->vfs_node);

        if (!(node->open_mode & WRITE_MODE)) {
            ctx.set_request_status(KErrAccessDenied);
            return;
        }

        int write_len = *ctx.get_arg<int>(1);
        int write_pos_provided = *ctx.get_arg<int>(2);

        std::uint64_t write_pos = 0;
        std::uint64_t last_pos = vfs_file->tell();
        bool should_reseek = false;

        write_pos = last_pos;

        // Low MaxUint64
        if (write_pos_provided != static_cast<int>(0x80000000)) {
            write_pos = write_pos_provided;
        }

        // If this write pos is beyond the current end of file, use last pos
        vfs_file->seek(write_pos > last_pos ? last_pos : write_pos, file_seek_mode::beg);
        size_t wrote_size = vfs_file->write_file(&(*write_data)[0], 1, write_len);

        LOG_TRACE("File {} wroted with size: {}",
            common::ucs2_to_utf8(vfs_file->file_name()), wrote_size);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_read(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::reinterpret_pointer_cast<file>(node->vfs_node);

        if (!(node->open_mode & READ_MODE)) {
            ctx.set_request_status(KErrAccessDenied);
            return;
        }

        int read_len = *ctx.get_arg<int>(1);
        int read_pos_provided = *ctx.get_arg<int>(2);

        std::uint64_t read_pos = 0;
        std::uint64_t last_pos = vfs_file->tell();
        bool should_reseek = false;

        read_pos = last_pos;

        // Low MaxUint64
        if (read_pos_provided != static_cast<int>(0x80000000)) {
            read_pos = read_pos_provided;
        }

        vfs_file->seek(read_pos, file_seek_mode::beg);

        uint64_t size = vfs_file->size();

        if (size - read_pos < read_len) {
            read_len = static_cast<int>(size - last_pos);
        }

        std::vector<char> read_data;
        read_data.resize(read_len);

        size_t read_finish_len = vfs_file->read_file(read_data.data(), 1, read_len);

        ctx.write_arg_pkg(0, reinterpret_cast<uint8_t *>(read_data.data()), read_len);

        LOG_TRACE("Readed {} from {} to address 0x{:x}", read_finish_len, read_pos, ctx.msg->args.args[0]);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_close(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::reinterpret_pointer_cast<file>(node->vfs_node);
        std::u16string path = vfs_file->file_name();

        vfs_file->close();

        // Delete temporary file
        if (node->temporary) {
            ctx.sys->get_io_system()->delete_entry(path);
        }

        nodes_table.close_nodes(*handle_res);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::is_file_in_rom(service::ipc_context ctx) {
        utf16_str &session_path = session_paths[ctx.msg->msg_session->unique_id()];
        std::optional<utf16_str> path = ctx.get_arg<utf16_str>(0);

        if (!path) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        auto final_path = std::move(*path);

        if (!eka2l1::is_absolute(final_path, session_path, true)) {
            final_path = eka2l1::absolute_path(final_path, session_path, true);
        }

        symfile f = ctx.sys->get_io_system()->open_file(final_path, READ_MODE);
        address addr = f->rom_address();

        f->close();

        ctx.write_arg_pkg<address>(1, addr);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::new_file_subsession(service::ipc_context ctx, bool overwrite, bool temporary) {
        std::optional<std::u16string> name_res = ctx.get_arg<std::u16string>(0);
        std::optional<int> open_mode_res = ctx.get_arg<int>(1);

        if (!name_res || !open_mode_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        *name_res = eka2l1::absolute_path(*name_res,
            session_paths[ctx.msg->msg_session->unique_id()]);

        std::string name_utf8 = common::ucs2_to_utf8(*name_res);

        {
            auto file_dir = eka2l1::file_directory(*name_res);

            // Do a check to return KErrPathNotFound
            if (!ctx.sys->get_io_system()->exist(file_dir)) {
                LOG_TRACE("Base directory of file {} not found", name_utf8);

                ctx.set_request_status(KErrPathNotFound);
                return;
            }
        }

        LOG_INFO("Opening file: {}", name_utf8);

        int handle = new_node(ctx.sys->get_io_system(), ctx.msg->own_thr, *name_res,
            *open_mode_res, overwrite, temporary);

        if (handle <= 0) {
            ctx.set_request_status(handle);
            return;
        }

        LOG_TRACE("Handle opended: {}", handle);

        ctx.write_arg_pkg<int>(3, handle);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_open(service::ipc_context ctx) {
        std::optional<std::u16string> name_res = ctx.get_arg<std::u16string>(0);
        std::optional<int> open_mode_res = ctx.get_arg<int>(1);

        if (!name_res || !open_mode_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        // LOG_TRACE("Opening exist {}", common::ucs2_to_utf8(*name_res));

        *name_res = eka2l1::absolute_path(*name_res,
            session_paths[ctx.msg->msg_session->unique_id()]);

        // Don't open file if it doesn't exist
        if (!ctx.sys->get_io_system()->exist(*name_res)) {
            LOG_TRACE("IO component not exists: {}", common::ucs2_to_utf8(*name_res));

            ctx.set_request_status(KErrNotFound);
            return;
        }

        new_file_subsession(ctx);
    }

    void fs_server::file_replace(service::ipc_context ctx) {
        new_file_subsession(ctx, true);
    }

    void fs_server::file_temp(service::ipc_context ctx) {
        auto dir_create = ctx.get_arg<std::u16string>(0);

        if (!dir_create) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        io_system *io = ctx.sys->get_io_system();

        auto full_path = eka2l1::absolute_path(*dir_create,
            session_paths[ctx.msg->msg_session->unique_id()]);

        if (!io->exist(full_path)) {
            LOG_TRACE("Directory for temp file not exists", common::ucs2_to_utf8(full_path));

            ctx.set_request_status(KErrPathNotFound);
            return;
        }

        std::u16string temp_name{ u"temp" };
        temp_name += common::utf8_to_ucs2(common::to_string(eka2l1::random_range(0, 0xFFFFFFFE), std::hex));

        full_path = eka2l1::add_path(full_path, temp_name);

        // Create the file if it doesn't exist
        symfile f = io->open_file(full_path, WRITE_MODE);
        f->close();

        LOG_INFO("Opening temp file: {}", common::ucs2_to_utf8(full_path));
        int handle = new_node(ctx.sys->get_io_system(), ctx.msg->own_thr, full_path,
            *ctx.get_arg<int>(1), true, true);

        if (handle <= 0) {
            ctx.set_request_status(handle);
            return;
        }

        LOG_TRACE("Handle opended: {}", handle);

        ctx.write_arg_pkg<int>(3, handle);

        // Arg2 take the temp path
        ctx.write_arg(2, full_path);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_create(service::ipc_context ctx) {
        std::optional<std::u16string> name_res = ctx.get_arg<std::u16string>(0);
        std::optional<int> open_mode_res = ctx.get_arg<int>(1);

        if (!name_res || !open_mode_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        // If the file already exist, stop
        if (ctx.sys->get_io_system()->exist(*name_res)) {
            ctx.set_request_status(KErrAlreadyExists);
            return;
        }

        new_file_subsession(ctx, true);
    }

    void fs_server::file_duplicate(service::ipc_context ctx) {
        int target_handle = *ctx.get_arg<int>(0);
        fs_node *node = nodes_table.get_node(target_handle);

        if (!node) {
            ctx.set_request_status(KErrNotFound);
            return;
        }

        size_t dup_handle = nodes_table.add_node(*node);

        ctx.write_arg_pkg<int>(3, static_cast<int>(dup_handle));
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_adopt(service::ipc_context ctx) {
        LOG_TRACE("Fs::FileAdopt stubbed");
        // TODO (pent0) : Do an adopt implementation

        ctx.set_request_status(KErrNone);
    }

    std::string replace_all(std::string str, const std::string &from, const std::string &to) {
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
        }
        return str;
    }

    std::regex construct_filter_from_wildcard(const utf16_str &filter) {
        std::string copy = common::ucs2_to_utf8(filter);

        std::locale loc("");

        for (auto &c : copy) {
            c = std::tolower(c, loc);
        }

        copy = replace_all(copy, "\\", "\\\\");
        copy = replace_all(copy, ".", std::string("\\") + ".");
        copy = replace_all(copy, "?", ".");
        copy = replace_all(copy, "*", ".*");

        return std::regex(copy);
    }

    void fs_server::notify_change(service::ipc_context ctx) {
        notify_entry entry;

        entry.match_pattern = ".*";
        entry.type = static_cast<notify_type>(*ctx.get_arg<int>(0));
        entry.request_status = ctx.msg->request_sts;
        entry.request_thread = ctx.msg->own_thr;

        notify_entries.push_back(entry);
    }

    void fs_server::notify_change_ex(service::ipc_context ctx) {
        std::optional<utf16_str> wildcard_match = ctx.get_arg<utf16_str>(1);

        if (!wildcard_match) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        notify_entry entry;
        entry.match_pattern = construct_filter_from_wildcard(*wildcard_match);
        entry.type = static_cast<notify_type>(*ctx.get_arg<int>(0));
        entry.request_status = ctx.msg->request_sts;
        entry.request_thread = ctx.msg->own_thr;

        notify_entries.push_back(entry);

        LOG_TRACE("Notify requested with wildcard: {}", common::ucs2_to_utf8(*wildcard_match));
    }

    int fs_server::new_node(io_system *io, thread_ptr sender, std::u16string name, int org_mode, bool overwrite, bool temporary) {
        int real_mode = org_mode & ~(epoc::EFileStreamText | epoc::EFileReadAsyncAll | epoc::EFileBigFile);
        fs_node_share share_mode = (fs_node_share)-1;

        if (real_mode & epoc::EFileShareExclusive) {
            share_mode = fs_node_share::exclusive;
        } else if (real_mode & epoc::EFileShareReadersOnly) {
            share_mode = fs_node_share::share_read;
        } else if (real_mode & epoc::EFileShareReadersOrWriters) {
            share_mode = fs_node_share::share_read_write;
        } else if (real_mode & epoc::EFileShareAny) {
            share_mode = fs_node_share::any;
        }

        // Fetch open mode
        int access_mode = -1;

        if (!(real_mode & epoc::EFileStreamText)) {
            access_mode = BIN_MODE;
        } else {
            access_mode = 0;
        }

        if (real_mode & epoc::EFileWrite) {
            if (overwrite) {
                access_mode |= WRITE_MODE;
            } else {
                access_mode |= APPEND_MODE;
            }
        } else {
            // Since EFileRead = 0, they default to read mode if nothing is specified more
            access_mode |= READ_MODE;
        }

        if (access_mode & WRITE_MODE && share_mode == fs_node_share::share_read) {
            return KErrAccessDenied;
        }

        kernel::owner_type owner_type = kernel::owner_type::kernel;

        // Fetch owner
        if (share_mode == fs_node_share::exclusive) {
            owner_type = kernel::owner_type::process;
        }

        fs_node *cache_node = nodes_table.get_node(name);

        if (!cache_node) {
            fs_node new_node;
            new_node.vfs_node = io->open_file(name, access_mode);
            new_node.temporary = temporary;

            if (!new_node.vfs_node) {
                LOG_TRACE("Can't open file {}", common::ucs2_to_utf8(name));
                return KErrNotFound;
            }

            if ((int)share_mode == -1) {
                share_mode = fs_node_share::share_read_write;
            }

            new_node.mix_mode = real_mode;
            new_node.open_mode = access_mode;
            new_node.share_mode = share_mode;
            new_node.own_process = sender->owning_process();

            std::size_t h = nodes_table.add_node(new_node);
            return (h == 0) ? KErrNoMemory : static_cast<int>(h);
        }

        if ((int)share_mode != -1 && share_mode != cache_node->share_mode) {
            if (share_mode == fs_node_share::exclusive || cache_node->share_mode == fs_node_share::exclusive) {
                return KErrAccessDenied;
            }

            // Compare mode, compatible ?
            if ((share_mode == fs_node_share::share_read && cache_node->share_mode == fs_node_share::any)
                || (share_mode == fs_node_share::share_read && cache_node->share_mode == fs_node_share::share_read_write && (cache_node->open_mode & WRITE_MODE))
                || (share_mode == fs_node_share::share_read_write && (access_mode & WRITE_MODE) && cache_node->share_mode == fs_node_share::share_read)
                || (share_mode == fs_node_share::any && cache_node->share_mode == fs_node_share::share_read)) {
                return KErrAccessDenied;
            }

            // Let's promote mode

            // Since we filtered incompatible mode, so if the share mode of them two is read, share mode of both of them is read
            if (share_mode == fs_node_share::share_read || cache_node->share_mode == fs_node_share::share_read) {
                share_mode = fs_node_share::share_read;
                cache_node->share_mode = fs_node_share::share_read;
            }

            if (share_mode == fs_node_share::any || cache_node->share_mode == fs_node_share::any) {
                share_mode = fs_node_share::any;
                cache_node->share_mode = fs_node_share::any;
            }

            if (share_mode == fs_node_share::share_read_write || share_mode == fs_node_share::share_read_write) {
                share_mode = fs_node_share::share_read_write;
                cache_node->share_mode = fs_node_share::share_read_write;
            }
        } else {
            share_mode = cache_node->share_mode;
        }

        if (share_mode == fs_node_share::share_read && access_mode & WRITE_MODE) {
            return KErrAccessDenied;
        }

        // Check if mode is compatible
        if (cache_node->share_mode == fs_node_share::exclusive) {
            // Check if process is the same
            // Deninded if mode is exclusive
            if (cache_node->own_process != sender->owning_process()) {
                LOG_TRACE("File open mode is exclusive, denide access");
                return KErrAccessDenied;
            }
        }

        // If we have the same open mode as the cache node, don't create new, returns this :D
        if (cache_node->open_mode == real_mode) {
            return cache_node->id;
        }

        fs_node new_node;
        new_node.vfs_node = io->open_file(name, access_mode);

        if (!new_node.vfs_node) {
            LOG_TRACE("Can't open file {}", common::ucs2_to_utf8(name));
            return KErrNotFound;
        }

        new_node.mix_mode = real_mode;
        new_node.open_mode = access_mode;
        new_node.share_mode = share_mode;

        std::size_t h = nodes_table.add_node(new_node);
        return (h == 0) ? KErrNoMemory : static_cast<int>(h);
    }

    bool is_e32img(symfile f) {
        int sig;

        f->seek(12, file_seek_mode::beg);
        f->read_file(&sig, 1, 4);

        if (sig != 0x434F5045) {
            return false;
        }

        return true;
    }

    void fs_server::mkdir(service::ipc_context ctx) {
        std::optional<std::u16string> dir = ctx.get_arg<std::u16string>(0);

        if (!dir) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        bool res = false;

        if (*ctx.get_arg<int>(1)) {
            res = ctx.sys->get_io_system()->create_directories(eka2l1::file_directory(*dir));
        } else {
            res = ctx.sys->get_io_system()->create_directory(eka2l1::file_directory(*dir));
        }

        if (!res) {
            // The guide specified: if it's parent does not exist or the sub-directory
            // already created, this should returns
            ctx.set_request_status(KErrAlreadyExists);
            return;
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::entry(service::ipc_context ctx) {
        std::optional<std::u16string> fname_op = ctx.get_arg<std::u16string>(0);

        if (!fname_op) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::u16string fname = std::move(*fname_op);

        LOG_INFO("Get entry of: {}", common::ucs2_to_utf8(fname));

        bool dir = false;

        io_system *io = ctx.sys->get_io_system();

        std::optional<entry_info> entry_hle = io->get_entry_info(fname);

        if (!entry_hle) {
            ctx.set_request_status(KErrNotFound);
            return;
        }

        epoc::TEntry entry;
        entry.aSize = static_cast<std::uint32_t>(entry_hle->size);

        if (entry_hle->has_raw_attribute) {
            entry.aAttrib = entry_hle->raw_attribute;
        } else {
            bool dir = (entry_hle->type == io_component_type::dir);

            if (static_cast<int>(entry_hle->attribute) & static_cast<int>(io_attrib::internal)) {
                entry.aAttrib = KEntryAttReadOnly | KEntryAttSystem;
            }

            // TODO (pent0): Mark the file as XIP if is ROM image (probably ROM already did it, but just be cautious).

            if (dir) {
                entry.aAttrib |= KEntryAttDir;
            } else {
                entry.aAttrib |= KEntryAttArchive;
            }
        }

        entry.aNameLength = static_cast<std::uint32_t>(fname.length());
        entry.aSizeHigh = 0; // This is never used, since the size is never >= 4GB as told by Nokia Doc

        memcpy(entry.aName, fname.data(), entry.aNameLength * 2);

        entry.aModified = epoc::TTime{ entry_hle->last_write };

        ctx.write_arg_pkg<epoc::TEntry>(1, entry);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::open_dir(service::ipc_context ctx) {
        auto dir = ctx.get_arg<std::u16string>(0);

        LOG_TRACE("Opening directory: {}", common::ucs2_to_utf8(*dir));

        if (!dir) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        const int attrib_raw = *ctx.get_arg<int>(1);
        io_attrib attrib = io_attrib::none;

        if (attrib_raw & KEntryAttDir) {
            attrib = attrib | io_attrib::include_dir;
        }

        fs_node node;
        node.vfs_node = ctx.sys->get_io_system()->open_dir(*dir, attrib);

        if (!node.vfs_node) {
            ctx.set_request_status(KErrPathNotFound);
            return;
        }

        node.own_process = ctx.msg->own_thr->owning_process();
        size_t dir_handle = nodes_table.add_node(node);

        struct uid_type {
            int uid[3];
        };

        uid_type type = *ctx.get_arg_packed<uid_type>(2);

        LOG_TRACE("UID requested: 0x{}, 0x{}, 0x{}", type.uid[0], type.uid[1], type.uid[2]);

        ctx.write_arg_pkg<int>(3, static_cast<int>(dir_handle));
        ctx.set_request_status(KErrNone);
    }

    void fs_server::close_dir(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::dir) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        nodes_table.close_nodes(*handle_res);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::read_dir(service::ipc_context ctx) {
        std::optional<int> handle = ctx.get_arg<int>(3);
        std::optional<int> entry_arr_vir_ptr = ctx.get_arg<int>(0);

        if (!handle || !entry_arr_vir_ptr) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *dir_node = nodes_table.get_node(*handle);

        if (!dir_node || dir_node->vfs_node->type != io_component_type::dir) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        std::shared_ptr<directory> dir = std::reinterpret_pointer_cast<directory>(
            dir_node->vfs_node);

        epoc::TEntry entry;
        std::optional<entry_info> info = dir->get_next_entry();

        if (!info) {
            ctx.set_request_status(KErrEof);
            return;
        }

        if (info->has_raw_attribute) {
            entry.aAttrib = info->raw_attribute;
        } else {
            switch (info->attribute) {
            case io_attrib::hidden: {
                entry.aAttrib = KEntryAttHidden;
                break;
            }

            default:
                break;
            }

            if (info->type == io_component_type::dir) {
                entry.aAttrib &= KEntryAttDir;
            } else {
                entry.aAttrib &= KEntryAttArchive;
            }
        }

        entry.aSize = static_cast<std::uint32_t>(info->size);
        entry.aNameLength = static_cast<std::uint32_t>(info->full_path.length());

        // TODO: Convert this using a proper function
        std::u16string path_u16(info->full_path.begin(), info->full_path.end());
        std::copy(path_u16.begin(), path_u16.end(), entry.aName);

        entry.aModified = epoc::TTime{ info->last_write };

        ctx.set_request_status(KErrNone);
    }

    void fs_server::read_dir_packed(service::ipc_context ctx) {
        std::optional<int> handle = ctx.get_arg<int>(3);
        std::optional<int> entry_arr_vir_ptr = ctx.get_arg<int>(0);

        if (!handle || !entry_arr_vir_ptr) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *dir_node = nodes_table.get_node(*handle);

        if (!dir_node || dir_node->vfs_node->type != io_component_type::dir) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        std::shared_ptr<directory> dir = std::reinterpret_pointer_cast<directory>(dir_node->vfs_node);

        process_ptr own_pr = ctx.msg->own_thr->owning_process();

        epoc::des8 *entry_arr = ptr<epoc::des8>(*entry_arr_vir_ptr).get(own_pr);
        epoc::buf_des<char> *entry_arr_buf = reinterpret_cast<epoc::buf_des<char> *>(entry_arr);

        TUint8 *entry_buf = reinterpret_cast<TUint8 *>(entry_arr->get_pointer(own_pr));
        TUint8 *entry_buf_end = entry_buf + entry_arr_buf->max_length;
        TUint8 *entry_buf_org = entry_buf;

        size_t queried_entries = 0;
        size_t entry_no_name_size = offsetof(epoc::TEntry, aName) + 8;

        while (entry_buf < entry_buf_end) {
            epoc::TEntry entry;
            std::optional<entry_info> info = dir->peek_next_entry();

            if (!info) {
                entry_arr->set_length(own_pr, static_cast<std::uint32_t>(entry_buf - entry_buf_org));

                ctx.set_request_status(KErrEof);

                return;
            }

            if (entry_buf + entry_no_name_size + common::align(common::utf8_to_ucs2(info->name).length() * 2, 4) + 4 > entry_buf_end) {
                break;
            }

            if (info->has_raw_attribute) {
                entry.aAttrib = info->raw_attribute;
            } else {
                switch (info->attribute) {
                case io_attrib::hidden: {
                    entry.aAttrib = KEntryAttHidden;
                    break;
                }

                default:
                    break;
                }

                if (info->type == io_component_type::dir) {
                    entry.aAttrib &= KEntryAttDir;
                } else {
                    entry.aAttrib &= KEntryAttArchive;
                }
            }

            entry.aSize = static_cast<std::uint32_t>(info->size);
            entry.aNameLength = static_cast<std::uint32_t>(info->name.length());

            // TODO: Convert this using a proper function
            std::u16string path_u16(info->name.begin(), info->name.end());
            std::copy(path_u16.begin(), path_u16.end(), entry.aName);

            entry.aModified = epoc::TTime{ info->last_write };

            memcpy(entry_buf, &entry, offsetof(epoc::TEntry, aName));
            entry_buf += offsetof(epoc::TEntry, aName);

            memcpy(entry_buf, &entry.aName[0], entry.aNameLength * 2);
            entry_buf += common::align(entry.aNameLength * 2, 4);

            if (kern->get_epoc_version() == epocver::epoc10) {
                // Epoc10 uses two reserved bytes
                memcpy(entry_buf, &entry.aSizeHigh, 8);
                entry_buf += 8;
            }

            queried_entries += 1;
            dir->get_next_entry();
        }

        entry_arr->set_length(own_pr, static_cast<std::uint32_t>(entry_buf - entry_buf_org));

        LOG_TRACE("Queried entries: 0x{:x}", queried_entries);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::drive_list(service::ipc_context ctx) {
        std::optional<int> flags = ctx.get_arg<int>(1);

        if (!flags) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::vector<io_attrib> exclude_attribs;
        std::vector<io_attrib> include_attribs;

        // Fetch flags
        if (*flags & KDriveAttHidden) {
            if (*flags & KDriveAttExclude) {
                exclude_attribs.push_back(io_attrib::hidden);
            } else {
                include_attribs.push_back(io_attrib::hidden);
            }
        }

        std::array<char, drive_count> dlist;

        std::fill(dlist.begin(), dlist.end(), 0);

        for (size_t i = drive_a; i < drive_count; i += 1) {
            auto drv_op = ctx.sys->get_io_system()->get_drive_entry(
                static_cast<drive_number>(i));

            if (drv_op) {
                eka2l1::drive drv = std::move(*drv_op);

                bool out = false;

                for (const auto &exclude : exclude_attribs) {
                    if (static_cast<int>(exclude) & static_cast<int>(drv.attribute)) {
                        dlist[i] = 0;
                        out = true;

                        break;
                    }
                }

                if (!out) {
                    if (include_attribs.empty()) {
                        if (drv.media_type != drive_media::none) {
                            dlist[i] = 1;
                        }

                        continue;
                    }

                    auto meet_one_condition = std::find_if(include_attribs.begin(), include_attribs.end(),
                        [=](io_attrib attrib) { return static_cast<int>(attrib) & static_cast<int>(drv.attribute); });

                    if (meet_one_condition != include_attribs.end()) {
                        dlist[i] = 1;
                    }
                }
            }
        }

        bool success = ctx.write_arg_pkg(0, reinterpret_cast<uint8_t *>(&dlist[0]),
            static_cast<std::uint32_t>(dlist.size()));

        if (!success) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::private_path(service::ipc_context ctx) {
        std::u16string path = u"\\private\\"
            + common::utf8_to_ucs2(common::to_string(std::get<2>(ctx.msg->own_thr->owning_process()->get_uid_type()), std::hex))
            + u"\\";

        ctx.write_arg(0, path);
        ctx.set_request_status(KErrNone);
    }

    void fill_drive_info(TDriveInfo *info, eka2l1::drive &io_drive) {
        info->iDriveAtt = 0;
        info->iMediaAtt = 0;

        if (io_drive.media_type == drive_media::none) {
            info->iType = EMediaUnknown;
            return;
        }

        switch (io_drive.media_type) {
        case drive_media::physical: {
            info->iType = EMediaHardDisk;
            info->iDriveAtt = KDriveAttLocal;

            break;
        }

        case drive_media::rom: {
            info->iType = EMediaRom;
            info->iDriveAtt = KDriveAttRom;

            break;
        }

        case drive_media::reflect: {
            info->iType = EMediaRotatingMedia;
            info->iDriveAtt = KDriveAttRedirected;

            break;
        }

        default:
            break;
        }

        info->iConnectionBusType = EConnectionBusInternal;
        info->iBattery = EBatNotSupported;

        if (static_cast<int>(io_drive.attribute & io_attrib::hidden)) {
            info->iDriveAtt |= KDriveAttHidden;
        }

        if (static_cast<int>(io_drive.attribute & io_attrib::internal)) {
            info->iDriveAtt |= KDriveAttInternal;
        }

        if (static_cast<int>(io_drive.attribute & io_attrib::removeable)) {
            info->iDriveAtt |= KDriveAttLogicallyRemovable;
        }

        if (static_cast<int>(io_drive.attribute & io_attrib::write_protected)) {
            info->iMediaAtt |= KMediaAttWriteProtected;
        }
    }

    /* Simple for now only, in the future this should be more advance. */
    void fs_server::drive(service::ipc_context ctx) {
        TDriveNumber drv = static_cast<TDriveNumber>(*ctx.get_arg<int>(1));
        std::optional<TDriveInfo> info = ctx.get_arg_packed<TDriveInfo>(0);

        if (!info) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::optional<eka2l1::drive> io_drive = ctx.sys->get_io_system()->get_drive_entry(static_cast<drive_number>(drv));

        if (!io_drive) {
            info->iType = EMediaUnknown;
        } else {
            fill_drive_info(&(*info), *io_drive);
        }

        ctx.write_arg_pkg<TDriveInfo>(0, *info);
        ctx.set_request_status(KErrNone);
    }

    enum TFileCacheFlags {
        EFileCacheReadEnabled = 0x01,
        EFileCacheReadOn = 0x02,
        EFileCacheReadAheadEnabled = 0x04,
        EFileCacheReadAheadOn = 0x08,
        EFileCacheWriteEnabled = 0x10,
        EFileCacheWriteOn = 0x20,
    };

    struct TVolumeInfo {
        TDriveInfo iDriveInfo;
        TUint iUniqueId;
        TInt64 iSize;
        TInt64 iFree;
        int iNameDesType;
        TUint16 iNameBuf[0x100];
        TFileCacheFlags iCacheFlags;
        TUint8 iVolSizeAsync;

        TUint8 i8Reserved1;
        TUint16 i16Reserved1;
        TUint32 i32Reserved1;
        TUint32 i32Reserved2;
    };

    void fs_server::volume(service::ipc_context ctx) {
        std::optional<TVolumeInfo> info = ctx.get_arg_packed<TVolumeInfo>(0);

        if (!info) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        TDriveNumber drv = static_cast<TDriveNumber>(*ctx.get_arg<int>(1));
        std::optional<eka2l1::drive> io_drive = ctx.sys->get_io_system()->get_drive_entry(static_cast<drive_number>(drv));

        if (!io_drive) {
            info->iDriveInfo.iType = EMediaUnknown;
        } else {
            fill_drive_info(&info->iDriveInfo, *io_drive);
        }

        info->iUniqueId = drv;

        LOG_WARN("Volume size stubbed with 1GB");

        // Stub this
        info->iSize = common::GB(1);
        info->iFree = common::GB(1);

        ctx.write_arg_pkg<TVolumeInfo>(0, *info);
        ctx.set_request_status(KErrNone);
    }

    struct TIoDriveParamInfo {
        TInt iBlockSize;
        TInt iClusterSize;
        TInt iRecReadBufSize;
        TInt iRecWriteBufSize;
        TUint64 iMaxSupportedFileSize;
    };

    void fs_server::query_drive_info_ext(service::ipc_context ctx) {
        drive_number drv = static_cast<drive_number>(*ctx.get_arg<int>(0));
        std::optional<eka2l1::drive> io_drive = ctx.sys->get_io_system()->get_drive_entry(drv);

        // If the drive hasn't been mounted yet, return KErrNotFound
        if (!io_drive) {
            ctx.set_request_status(KErrNotFound);
            return;
        }

        extended_fs_query_command query_cmd = static_cast<extended_fs_query_command>(*ctx.get_arg<int>(1));

        switch (query_cmd) {
        case extended_fs_query_command::file_system_sub_type: {
            // Query file system type. Using FAT32 as default.
            ctx.write_arg(2, u"FAT32");
            break;
        }

        case extended_fs_query_command::is_drive_sync: {
            // Check if drive is sync. Yes in this case.
            ctx.write_arg_pkg(2, true);
            break;
        }

        case extended_fs_query_command::is_drive_finalised: {
            // Check if drive is safe to remove. Yes ?
            LOG_WARN("Checking if drive is finalised, stubbed");
            ctx.write_arg_pkg(2, true);
            break;
        }

        case extended_fs_query_command::io_param_info: {
            TIoDriveParamInfo param;
            param.iBlockSize = 512;
            param.iClusterSize = 4096;
            param.iMaxSupportedFileSize = 0xFFFFFFFF;
            param.iRecReadBufSize = 8192;
            param.iRecWriteBufSize = 16384;

            LOG_INFO("IOParamInfo stubbed");
            ctx.write_arg_pkg(2, param);

            break;
        }

        default: {
            LOG_ERROR("Unimplemented extended query drive opcode: 0x{:x}", static_cast<int>(query_cmd));
            break;
        }
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::set_should_notify_failure(service::ipc_context ctx) {
        should_notify_failures = static_cast<bool>(ctx.get_arg<int>(0));
        ctx.set_request_status(KErrNone);
    }
}