#include <Storages/ExternalDataSourceConfiguration.h>

#include "MongoDBDictionarySource.h"
#include "DictionarySourceFactory.h"
#include "DictionaryStructure.h"

#include <bsoncxx/builder/basic/array.hpp>

namespace DB
{

static const std::unordered_set<std::string_view> dictionary_allowed_keys = {
    "host", "port", "user", "password", "db", "database", "uri", "collection", "name", "options"};

void registerDictionarySourceMongoDB(DictionarySourceFactory & factory)
{
    auto create_mongo_db_dictionary = [](
        const DictionaryStructure & dict_struct,
        const Poco::Util::AbstractConfiguration & config,
        const std::string & root_config_prefix,
        Block & sample_block,
        ContextPtr context,
        const std::string & /* default_database */,
        bool created_from_ddl)
    {
        const auto config_prefix = root_config_prefix + ".mongodb";
        ExternalDataSourceConfiguration configuration;
        auto has_config_key = [](const String & key) { return dictionary_allowed_keys.contains(key); };
        auto named_collection = getExternalDataSourceConfiguration(config, config_prefix, context, has_config_key);
        if (named_collection)
        {
            configuration = named_collection->configuration;
        }
        else
        {
            configuration.host = config.getString(config_prefix + ".host", "");
            configuration.port = config.getUInt(config_prefix + ".port", 0);
            configuration.username = config.getString(config_prefix + ".user", "");
            configuration.password = config.getString(config_prefix + ".password", "");
            configuration.database = config.getString(config_prefix + ".db", "");
        }

        if (created_from_ddl)
            context->getRemoteHostFilter().checkHostAndPort(configuration.host, toString(configuration.port));

        return std::make_unique<MongoDBDictionarySource>(dict_struct,
            config.getString(config_prefix + ".uri", ""),
            configuration.host,
            configuration.port,
            configuration.username,
            configuration.password,
            configuration.database,
            config.getString(config_prefix + ".collection"),
            config.getString(config_prefix + ".options", ""),
            sample_block);
    };

    factory.registerSource("mongodb", create_mongo_db_dictionary);
}

}

#include <Common/logger_useful.h>
#include <IO/WriteHelpers.h>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

namespace DB
{
namespace ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
    extern const int UNSUPPORTED_METHOD;
    extern const int MONGODB_CANNOT_AUTHENTICATE;
}


static const UInt64 max_block_size = 8192;


MongoDBDictionarySource::MongoDBDictionarySource(
    const DictionaryStructure & dict_struct_,
    const std::string & uri_str_,
    const std::string & host_,
    const UInt16 & port_,
    const std::string & username_,
    const std::string & password_,
    const std::string & database_name_,
    const std::string & collection_name_,
    const std::string & options_,
    Block & sample_block_)
    : dict_struct{dict_struct_}
    , uri_str{uri_str_}
    , host{host_}
    , port{port_}
    , username{username_}
    , password{password_}
    , database_name{database_name_}
    , collection_name{collection_name_}
    , options{options_}
    , sample_block{sample_block_}
{
    if (!uri_str.empty())
    {
        uri = mongocxx::uri{uri_str};
        host = uri.hosts()[0].name;
        port = uri.hosts()[0].port;
        username = uri.username();
        database_name = uri.database();
    }
    else
        uri = mongocxx::uri{"mongodb://" + username_ + ":" + password_ + "@" + host_ + ":" + std::to_string(port_) + "/" + database_name_ + "?" + options_};
}


MongoDBDictionarySource::MongoDBDictionarySource(const MongoDBDictionarySource & other)
    : MongoDBDictionarySource{other.dict_struct, other.uri_str, other.host, other.port, other.username, other.password, other.database_name,
                              other.collection_name, other.options, other.sample_block}
{
}

MongoDBDictionarySource::~MongoDBDictionarySource() = default;

QueryPipeline MongoDBDictionarySource::loadAll()
{
    return QueryPipeline(std::make_shared<MongoDBSource>(uri, database_name, collection_name,
                                                         make_document(), mongocxx::options::find{}, sample_block, max_block_size));
}

QueryPipeline MongoDBDictionarySource::loadIds(const std::vector<UInt64> & ids)
{
    if (!dict_struct.id)
        throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "'id' is required for selective loading");

    auto ids_array = bsoncxx::builder::basic::array{};
    for(const auto & id : ids)
        ids_array.append(static_cast<Int64>(id));

    auto query = make_document(kvp(dict_struct.id->name,  make_document(kvp("$in", ids_array))));

    return QueryPipeline(std::make_shared<MongoDBSource>(uri, database_name, collection_name,
                                                         query.view(), mongocxx::options::find{}, sample_block, max_block_size));
}


QueryPipeline MongoDBDictionarySource::loadKeys(const Columns & /*key_columns*/, const std::vector<size_t> & /*requested_rows*/)
{
    /*if (!dict_struct.key)
        throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "'key' is required for selective loading");

    Poco::MongoDB::Document query;
    Poco::MongoDB::Array::Ptr keys_array(new Poco::MongoDB::Array);

    for (const auto row_idx : requested_rows)
    {
        auto & key = keys_array->addNewDocument(DB::toString(row_idx));

        const auto & key_attributes = *dict_struct.key;
        for (size_t attribute_index = 0; attribute_index < key_attributes.size(); ++attribute_index)
        {
            const auto & key_attribute = key_attributes[attribute_index];

            switch (key_attribute.underlying_type)
            {
                case AttributeUnderlyingType::UInt8:
                case AttributeUnderlyingType::UInt16:
                case AttributeUnderlyingType::UInt32:
                case AttributeUnderlyingType::UInt64:
                case AttributeUnderlyingType::Int8:
                case AttributeUnderlyingType::Int16:
                case AttributeUnderlyingType::Int32:
                case AttributeUnderlyingType::Int64:
                {
                    key.add(key_attribute.name, static_cast<Int32>(key_columns[attribute_index]->get64(row_idx)));
                    break;
                }
                case AttributeUnderlyingType::Float32:
                case AttributeUnderlyingType::Float64:
                {
                    key.add(key_attribute.name, key_columns[attribute_index]->getFloat64(row_idx));
                    break;
                }
                case AttributeUnderlyingType::String:
                {
                    String loaded_str((*key_columns[attribute_index])[row_idx].get<String>());
                    /// Convert string to ObjectID
                    if (key_attribute.is_object_id)
                    {
                        Poco::MongoDB::ObjectId::Ptr loaded_id(new Poco::MongoDB::ObjectId(loaded_str));
                        key.add(key_attribute.name, loaded_id);
                    }
                    else
                    {
                        key.add(key_attribute.name, loaded_str);
                    }
                    break;
                }
                default:
                    throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Unsupported dictionary attribute type for MongoDB dictionary source");
            }
        }
    }

    /// If more than one key we should use $or
    query.add("$or", keys_array);*/

    return QueryPipeline{};
    //return QueryPipeline(std::make_shared<MongoDBSource>(connection, db, collection, query, sample_block, max_block_size));
}

std::string MongoDBDictionarySource::toString() const
{
    return fmt::format("MongoDB: {}.{},{}{}:{}", database_name, collection_name, (username.empty() ? " " : " " + username + '@'), host, port);
}

}
