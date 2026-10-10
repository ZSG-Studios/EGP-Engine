// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/result.hpp"
#include <sqlite3.h>
#include <cstddef>
#include <span>
#include <string_view>

namespace superpos::detail {
// Returned views remain owned by the statement. Do not step/reset/finalize it,
// or convert the same column, until the view has been consumed. Materialize
// before asking for the byte count, and inspect a suspect result immediately:
// a later SQLite API call can replace the connection's allocation error.
inline Result<std::span<const std::byte>> sqlite_blob(sqlite3_stmt* statement,int column,
    std::size_t minimum,std::size_t maximum) noexcept {
    if(!statement||column<0||column>=sqlite3_column_count(statement)||minimum>maximum)return fail(Error::InvalidArgument);
    auto* database=sqlite3_db_handle(statement);
    if(sqlite3_column_type(statement,column)!=SQLITE_BLOB)return fail(Error::RecoveryUnavailable);
    const auto* data=static_cast<const std::byte*>(sqlite3_column_blob(statement,column));
    if(!data&&sqlite3_errcode(database)==SQLITE_NOMEM)return fail(Error::OutOfMemory);
    const auto bytes=sqlite3_column_bytes(statement,column);
    if(bytes==0&&sqlite3_errcode(database)==SQLITE_NOMEM)return fail(Error::OutOfMemory);
    if(bytes<0||static_cast<std::size_t>(bytes)<minimum||static_cast<std::size_t>(bytes)>maximum||(!data&&bytes))return fail(Error::RecoveryUnavailable);
    return std::span<const std::byte>(data,static_cast<std::size_t>(bytes));
}
inline Result<std::string_view> sqlite_text(sqlite3_stmt* statement,int column,
    std::size_t maximum) noexcept {
    if(!statement||column<0||column>=sqlite3_column_count(statement))return fail(Error::InvalidArgument);
    auto* database=sqlite3_db_handle(statement);
    if(sqlite3_column_type(statement,column)!=SQLITE_TEXT)return fail(Error::RecoveryUnavailable);
    const auto* data=reinterpret_cast<const char*>(sqlite3_column_text(statement,column));
    if(!data&&sqlite3_errcode(database)==SQLITE_NOMEM)return fail(Error::OutOfMemory);
    const auto bytes=sqlite3_column_bytes(statement,column);
    if(bytes==0&&sqlite3_errcode(database)==SQLITE_NOMEM)return fail(Error::OutOfMemory);
    if(bytes<0||static_cast<std::size_t>(bytes)>maximum||!data)return fail(Error::RecoveryUnavailable);
    return std::string_view(data,static_cast<std::size_t>(bytes));
}
}
