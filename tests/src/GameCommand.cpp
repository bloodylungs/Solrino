#include "providers/twitch/ChannelManagement.hpp"
#include "Test.hpp"

using namespace chatterino;

TEST(GameCommand, lowercasesTheCategoryName)
{
    EXPECT_EQ(ChannelManagement::gameCommandMessage("Fortnite"),
              "!game fortnite");
}

TEST(GameCommand, keepsWordsSeparatedBySpaces)
{
    EXPECT_EQ(ChannelManagement::gameCommandMessage("League of Legends"),
              "!game league of legends");
}

TEST(GameCommand, keepsPunctuationAndAccents)
{
    EXPECT_EQ(ChannelManagement::gameCommandMessage(
                  QString::fromUtf8("Pok\xC3\xA9mon: Red & Blue")),
              QString::fromUtf8("!game pok\xC3\xA9mon: red & blue"));
}

TEST(GameCommand, collapsesWhitespaceAndLineBreaks)
{
    EXPECT_EQ(ChannelManagement::gameCommandMessage("  Just \n\t Chatting  "),
              "!game just chatting");
}

TEST(GameCommand, emptyNameProducesNoMessage)
{
    EXPECT_TRUE(ChannelManagement::gameCommandMessage("").isEmpty());
    EXPECT_TRUE(ChannelManagement::gameCommandMessage(" \n ").isEmpty());
    EXPECT_TRUE(ChannelManagement::gameCommandMessage("", "!setgame").isEmpty());
}

TEST(GameCommand, usesTheChannelsOwnCommand)
{
    EXPECT_EQ(ChannelManagement::gameCommandMessage("Fortnite", "!setgame"),
              "!setgame fortnite");
    EXPECT_EQ(ChannelManagement::gameCommandMessage("Fortnite", "  $Game  "),
              "$Game fortnite");
}

TEST(GameCommand, blankCommandMeansTheDefault)
{
    EXPECT_EQ(ChannelManagement::gameCommandMessage("Fortnite", "   "),
              "!game fortnite");
}
