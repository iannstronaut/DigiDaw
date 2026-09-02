#pragma once

#include <string_view>
#include <cstdint>

namespace digidaw::domain {

enum class ErrorCategory : uint8_t {
    Audio = 1,
    Project = 2,
    Plugin = 3,
    Script = 4,
    Network = 5,
    Config = 6,
    System = 7
};

enum class ErrorCode : uint32_t {
    Success = 0,

    // Audio (ERR-AUD)
    DeviceOpenFailed = 1001,
    DeviceLost = 1002,
    BufferUnderrun = 1003,
    SampleRateMismatch = 1004,
    AsioDriverBroken = 1005,

    // Project (ERR-PRJ)
    UnsupportedVersion = 2001,
    CorruptChunk = 2002,
    ChecksumMismatch = 2003,
    FileLocked = 2004,
    AutosaveFailed = 2005,
    RecoveryAvailable = 2006,
    FileNotFound = 2007,
    InvalidMagic = 2008,

    // Plugin (ERR-PLG)
    ScanFailed = 3001,
    ScanTimeout = 3002,
    BridgeCrash = 3003,
    BridgeHang = 3004,
    StateIncompatible = 3005,
    MissingPlugin = 3006,
    NodeBypassed = 3007,

    // Script (ERR-SCR)
    CompileError = 4001,
    RuntimeException = 4002,
    CpuOverrun = 4003,
    ImportBlocked = 4004,

    // Network (ERR-NET)
    Offline = 5001,
    FeedError = 5002,
    AuthFailed = 5003,
    UpdateCheckFailed = 5004,

    // Config (ERR-CFG)
    RegistryDenied = 6001,
    CorruptValue = 6002,
    UserTreeMissing = 6003,

    // System (ERR-SYS)
    OutOfMemory = 7001,
    EngineSignature = 7002,
    FatalCrash = 7003,
    FileAssociationLost = 7004,
    InvalidArgument = 7005,
    GraphCycleDetected = 7006
};

constexpr std::string_view get_error_message(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success: return "Operasi berhasil";
        case ErrorCode::DeviceOpenFailed: return "Perangkat audio tidak bisa dibuka";
        case ErrorCode::DeviceLost: return "Perangkat audio terputus";
        case ErrorCode::BufferUnderrun: return "CPU/beban audio terlalu tinggi";
        case ErrorCode::SampleRateMismatch: return "Sample rate mismatch";
        case ErrorCode::AsioDriverBroken: return "Driver ASIO bermasalah";
        case ErrorCode::UnsupportedVersion: return "Project dibuat versi lebih baru";
        case ErrorCode::CorruptChunk: return "Bagian project rusak";
        case ErrorCode::ChecksumMismatch: return "Checksum project mismatch";
        case ErrorCode::FileLocked: return "File dipakai proses lain";
        case ErrorCode::AutosaveFailed: return "Autosave gagal menyimpan file";
        case ErrorCode::RecoveryAvailable: return "Recovery project ditemukan";
        case ErrorCode::FileNotFound: return "File tidak ditemukan";
        case ErrorCode::InvalidMagic: return "Format project tidak valid (magic header mismatch)";
        case ErrorCode::ScanFailed: return "Plugin tidak bisa dimuat";
        case ErrorCode::ScanTimeout: return "Plugin scan timeout";
        case ErrorCode::BridgeCrash: return "Plugin crash — dibypass";
        case ErrorCode::BridgeHang: return "Plugin hang — bridge tidak merespons";
        case ErrorCode::StateIncompatible: return "State plugin berasal versi lain";
        case ErrorCode::MissingPlugin: return "Plugin tidak terpasang";
        case ErrorCode::NodeBypassed: return "Node plugin dibypass";
        case ErrorCode::CompileError: return "Script error (detail di log)";
        case ErrorCode::RuntimeException: return "Script runtime exception";
        case ErrorCode::CpuOverrun: return "Script terlalu berat — dihentikan";
        case ErrorCode::ImportBlocked: return "Script mencoba akses terlarang";
        case ErrorCode::Offline: return "Koneksi offline — fallback lokal digunakan";
        case ErrorCode::FeedError: return "Gagal membaca RSS feed";
        case ErrorCode::AuthFailed: return "Autentikasi gagal";
        case ErrorCode::UpdateCheckFailed: return "Gagal memeriksa pembaruan";
        case ErrorCode::RegistryDenied: return "Akses registry ditolak — fallback ke file";
        case ErrorCode::CorruptValue: return "Nilai konfigurasi korup";
        case ErrorCode::UserTreeMissing: return "Direktori dokumen belum dibuat";
        case ErrorCode::OutOfMemory: return "Memori tidak cukup";
        case ErrorCode::EngineSignature: return "Integritas engine gagal";
        case ErrorCode::FatalCrash: return "Crash fatal terdeteksi";
        case ErrorCode::FileAssociationLost: return "Asosiasi file terputus";
        case ErrorCode::InvalidArgument: return "Argumen fungsi tidak valid";
        case ErrorCode::GraphCycleDetected: return "Siklus terdeteksi dalam mixer routing";
        default: return "Error tidak dikenal";
    }
}

constexpr std::string_view get_error_identifier(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success: return "ERR-NONE";
        case ErrorCode::DeviceOpenFailed: return "ERR-AUD-001";
        case ErrorCode::DeviceLost: return "ERR-AUD-002";
        case ErrorCode::BufferUnderrun: return "ERR-AUD-003";
        case ErrorCode::SampleRateMismatch: return "ERR-AUD-004";
        case ErrorCode::AsioDriverBroken: return "ERR-AUD-005";
        case ErrorCode::UnsupportedVersion: return "ERR-PRJ-001";
        case ErrorCode::CorruptChunk: return "ERR-PRJ-002";
        case ErrorCode::ChecksumMismatch: return "ERR-PRJ-003";
        case ErrorCode::FileLocked: return "ERR-PRJ-004";
        case ErrorCode::AutosaveFailed: return "ERR-PRJ-005";
        case ErrorCode::RecoveryAvailable: return "ERR-PRJ-006";
        case ErrorCode::FileNotFound: return "ERR-PRJ-007";
        case ErrorCode::InvalidMagic: return "ERR-PRJ-008";
        case ErrorCode::ScanFailed: return "ERR-PLG-001";
        case ErrorCode::ScanTimeout: return "ERR-PLG-002";
        case ErrorCode::BridgeCrash: return "ERR-PLG-003";
        case ErrorCode::BridgeHang: return "ERR-PLG-004";
        case ErrorCode::StateIncompatible: return "ERR-PLG-005";
        case ErrorCode::MissingPlugin: return "ERR-PLG-006";
        case ErrorCode::NodeBypassed: return "ERR-PLG-007";
        case ErrorCode::CompileError: return "ERR-SCR-001";
        case ErrorCode::RuntimeException: return "ERR-SCR-002";
        case ErrorCode::CpuOverrun: return "ERR-SCR-003";
        case ErrorCode::ImportBlocked: return "ERR-SCR-004";
        case ErrorCode::Offline: return "ERR-NET-001";
        case ErrorCode::FeedError: return "ERR-NET-002";
        case ErrorCode::AuthFailed: return "ERR-NET-003";
        case ErrorCode::UpdateCheckFailed: return "ERR-NET-004";
        case ErrorCode::RegistryDenied: return "ERR-CFG-001";
        case ErrorCode::CorruptValue: return "ERR-CFG-002";
        case ErrorCode::UserTreeMissing: return "ERR-CFG-003";
        case ErrorCode::OutOfMemory: return "ERR-SYS-001";
        case ErrorCode::EngineSignature: return "ERR-SYS-002";
        case ErrorCode::FatalCrash: return "ERR-SYS-003";
        case ErrorCode::FileAssociationLost: return "ERR-SYS-004";
        case ErrorCode::InvalidArgument: return "ERR-SYS-005";
        case ErrorCode::GraphCycleDetected: return "ERR-SYS-006";
        default: return "ERR-UNKNOWN";
    }
}

} // namespace digidaw::domain
