#include <AYSequence/DialogueTextPresenter.h>
#include <AYLocalization.h>
#include <AYUI/TextLabel.h>
#include <AYTest.h>

using namespace ayt::sequence;
namespace dialogue=ayt::progression::dialogue;

TEST_SUITE(DialogueTextPresenterTests)
TEST_CASE(committed_restored_and_language_changed_lines_share_one_owned_ui_pair) {
    auto& locale=ayt::localization::Localization::instance();
    locale.clear();
    locale.addSupportedLanguage("en-US");
    locale.setLanguage("en-US");
    locale.setText("line.hello","Hello");
    locale.setText("speaker.guard","Guard");
    ayt::ui::TextLabel speaker,body;
    DialogueTextPresenter presenter;
    CHECK_TRUE(presenter.bind(speaker,body));
    dialogue::Presentation frame;
    frame.deliveryKey="view:guard:0";
    frame.textKey="line.hello";
    frame.speakerKey="speaker.guard";
    frame.startMedia=true;
    CHECK_TRUE(presenter.apply(frame,locale));
    CHECK(body.isVisible() && body.getText()==L"Hello");
    CHECK(speaker.isVisible() && speaker.getText()==L"Guard");
    frame.startMedia=false; // Restored cursor refreshes text, not media.
    locale.setText("line.hello","Welcome");
    CHECK_TRUE(presenter.apply(frame,locale));
    CHECK(body.getText()==L"Welcome");
    frame.deliveryKey="view:guard:1";
    frame.textKey="line.goodbye";
    frame.speakerKey.clear();
    CHECK_TRUE(presenter.apply(frame,locale));
    CHECK(body.getText()==L"line.goodbye" && !speaker.isVisible());
    frame.textKey.clear();
    CHECK_TRUE(presenter.apply(frame,locale));
    CHECK_FALSE(body.isVisible());
    locale.clear();
}
TEST_CASE(invalid_binding_preserves_existing_labels) {
    auto& locale=ayt::localization::Localization::instance();
    locale.clear();
    ayt::ui::TextLabel speaker,body;
    DialogueTextPresenter presenter;
    CHECK_TRUE(presenter.bind(speaker,body));
    dialogue::Presentation frame;
    frame.deliveryKey="view:one";
    frame.textKey="line.one";
    CHECK_TRUE(presenter.apply(frame,locale));
    CHECK_FALSE(presenter.bind(body,body));
    CHECK(body.isVisible() && body.getText()==L"line.one");
    presenter.clear();
    CHECK_FALSE(body.isVisible());
}
TEST_SUITE_END
