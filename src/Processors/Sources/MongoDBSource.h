#pragma once

#include <Core/Block.h>
#include <Core/Field.h>
#include <Core/ExternalResultDescription.h>
#include <Processors/ISource.h>

#include <bsoncxx/document/element.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/cursor.hpp>

namespace DB
{

/// Creates MongoDB connection and cursor, converts it to a stream of blocks
class MongoDBSource final : public ISource
{
public:
    MongoDBSource(
        const mongocxx::uri & uri,
        const std::string & database_name,
        const std::string & collection_name,
        const bsoncxx::v_noabi::document::view_or_value & query,
        const mongocxx::options::find & options,
        Block & header_,
        const UInt64 & max_block_size_);

    ~MongoDBSource() override;

    String getName() const override { return "MongoDB"; }

private:
    Chunk generate() override;

    mongocxx::client client;
    mongocxx::database database;
    mongocxx::collection collection;
    mongocxx::cursor cursor;

    Block & header;
    const UInt64 max_block_size;

    bool all_read = false;
    ExternalResultDescription description;
};

}
