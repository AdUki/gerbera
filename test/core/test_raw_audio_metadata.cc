/*GRB*
    Gerbera - https://gerbera.io/

    Copyright (C) 2026 Gerbera Contributors

    Gerbera is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    Gerbera is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Gerbera. If not, see <http://www.gnu.org/licenses/>.
*/

#include "cds/cds_item.h"
#include "cds/cds_resource.h"
#include "config/config_definition.h"
#include "config/config_val.h"
#include "config/result/directory_tweak.h"
#include "config/setup/config_setup_dictionary.h"
#include "context.h"
#include "metadata/ffmpeg_handler.h"
#include "metadata/metadata_enums.h"
#include "metadata/taglib_handler.h"
#include "upnp/upnp_common.h"
#include "util/mime.h"
#include "util/string_converter.h"

#include "../mock/config_mock.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pugixml.hpp>
#include <tuple>

static std::shared_ptr<ConfigDefinition> rawAudioDefinition()
{
    auto definition = std::make_shared<ConfigDefinition>();
    definition->init(definition);
    return definition;
}

static std::map<std::string, std::string> rawAudioDefaultMappings(ConfigVal option)
{
    auto setup = rawAudioDefinition()->findConfigSetup<ConfigDictionarySetup>(option);
    return setup->getXmlContent({}, nullptr);
}

TEST(RawAudioMetadataMappingsTest, MapsRawDolbyAndDtsMimetypesToRawAudio)
{
    auto contentTypes = rawAudioDefaultMappings(ConfigVal::IMPORT_MAPPINGS_MIMETYPE_TO_CONTENTTYPE_LIST);
    for (auto&& mimeType : { "audio/ac3", "audio/eac3", "audio/vnd.dolby.dd-raw", "audio/vnd.dolby.mlp", "audio/vnd.dts", "audio/vnd.dts.hd" }) {
        ASSERT_EQ(contentTypes.count(mimeType), 1) << mimeType;
        EXPECT_EQ(contentTypes.at(mimeType), CONTENT_TYPE_RAW_AUDIO) << mimeType;
    }
}

class RawAudioMetadataConfig final : public ConfigMock {
public:
    std::string getOption(ConfigVal option) const override
    {
        if (option == ConfigVal::IMPORT_LIBOPTS_ENTRY_SEP)
            return "; ";
        if (option == ConfigVal::IMPORT_LIBOPTS_ENTRY_LEGACY_SEP)
            return "\n";
#ifdef HAVE_MAGIC
        if (option == ConfigVal::IMPORT_MAGIC_FILE)
            return "";
#endif
        return "UTF-8";
    }

    bool getBoolOption(ConfigVal option) const override
    {
#ifdef HAVE_FFMPEG
        if (option == ConfigVal::IMPORT_LIBOPTS_FFMPEG_ENABLED)
            return true;
#endif
#ifdef HAVE_TAGLIB
        if (option == ConfigVal::IMPORT_LIBOPTS_ID3_ENABLED)
            return true;
#endif
        return ConfigMock::getBoolOption(option);
    }

    std::map<std::string, std::string> getDictionaryOption(ConfigVal option) const override
    {
        if (option == ConfigVal::IMPORT_MAPPINGS_MIMETYPE_TO_CONTENTTYPE_LIST)
            return rawAudioDefaultMappings(option);
        return ConfigMock::getDictionaryOption(option);
    }

    std::shared_ptr<DirectoryConfigList> getDirectoryTweakOption(ConfigVal) const override
    {
        return std::make_shared<DirectoryConfigList>();
    }
};

#ifdef HAVE_TAGLIB
class RawAudioTagLibMetadataTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto config = std::make_shared<RawAudioMetadataConfig>();
        auto converter = std::make_shared<ConverterManager>(config);
        auto mime = std::make_shared<Mime>(config);
        context = std::make_shared<Context>(nullptr, config, nullptr, mime, nullptr, nullptr, converter);
    }

    static std::shared_ptr<CdsItem> makeItem(const fs::path& path, const std::string& mimeType)
    {
        auto item = std::make_shared<CdsItem>(CdsEntryType::File);
        item->setLocation(fs::absolute(path), CdsEntryType::File);
        item->setMimeType(mimeType);
        item->setClass(UPNP_CLASS_MUSIC_TRACK);
        auto resource = std::make_shared<CdsResource>(ContentHandler::DEFAULT, ResourcePurpose::Content);
        resource->addAttribute(ResourceAttribute::PROTOCOLINFO, fmt::format("http-get:*:{}:*", mimeType));
        resource->addAttribute(ResourceAttribute::SIZE, static_cast<long long>(fs::file_size(path)));
        item->addResource(resource);
        return item;
    }

    std::shared_ptr<CdsItem> read(const fs::path& path, const std::string& mimeType)
    {
        auto item = makeItem(path, mimeType);
        TagLibHandler handler(context);
        EXPECT_TRUE(handler.isEnabled(CONTENT_TYPE_RAW_AUDIO));
        EXPECT_TRUE(handler.isSupported(CONTENT_TYPE_RAW_AUDIO, false, mimeType, item->getMediaType()));
        std::vector<int> newIds;
        EXPECT_TRUE(handler.fillMetadata(item, newIds));
        return item;
    }

    static void expectTags(const std::shared_ptr<CdsItem>& item, const std::string& title)
    {
        EXPECT_EQ(item->getMetaData(MetadataFields::M_TITLE), title);
        EXPECT_EQ(item->getMetaData(MetadataFields::M_ARTIST), "Raw artist");
        EXPECT_EQ(item->getMetaData(MetadataFields::M_ALBUMARTIST), "Raw album artist");
        EXPECT_EQ(item->getMetaData(MetadataFields::M_ALBUM), "Raw album");
        EXPECT_EQ(item->getMetaData(MetadataFields::M_GENRE), "Test");
        EXPECT_EQ(item->getTrackNumber(), 3);
    }

    std::shared_ptr<Context> context;
};

TEST_F(RawAudioTagLibMetadataTest, ReadsId3v2TagOfRawEac3)
{
    expectTags(read("fixtures/raw-id3.eac3", "audio/vnd.dolby.dd-raw"), "Raw E-AC-3 regression");
}

TEST_F(RawAudioTagLibMetadataTest, ReadsId3v2TagOfRawTrueHd)
{
    expectTags(read("fixtures/raw-id3.thd", "audio/vnd.dolby.mlp"), "Raw TrueHD regression");
}

TEST_F(RawAudioTagLibMetadataTest, LeavesUntaggedRawStreamEmpty)
{
    auto item = read("fixtures/raw-no-id3.eac3", "audio/vnd.dolby.dd-raw");
    EXPECT_TRUE(item->getMetaData(MetadataFields::M_TITLE).empty());
    EXPECT_TRUE(item->getMetaData(MetadataFields::M_ARTIST).empty());
    EXPECT_TRUE(item->getMetaData(MetadataFields::M_ALBUM).empty());
}

#ifdef HAVE_FFMPEG
TEST_F(RawAudioTagLibMetadataTest, KeepsFfmpegStreamPropertiesAfterTagLib)
{
    for (auto&& [path, mimeType, title] : {
             std::tuple { "fixtures/raw-id3.eac3", "audio/vnd.dolby.dd-raw", "Raw E-AC-3 regression" },
             std::tuple { "fixtures/raw-id3.thd", "audio/vnd.dolby.mlp", "Raw TrueHD regression" },
         }) {
        auto item = read(path, mimeType);
        FfmpegHandler ffmpeg(context);
        std::vector<int> newIds;
        EXPECT_TRUE(ffmpeg.fillMetadata(item, newIds)) << path;
        expectTags(item, title);
        auto res = item->getResource(ContentHandler::DEFAULT);
        EXPECT_EQ(res->getAttribute(ResourceAttribute::NRAUDIOCHANNELS), "2") << path;
        EXPECT_EQ(res->getAttribute(ResourceAttribute::SAMPLEFREQUENCY), "48000") << path;
    }
}
#endif // HAVE_FFMPEG
#endif // HAVE_TAGLIB
