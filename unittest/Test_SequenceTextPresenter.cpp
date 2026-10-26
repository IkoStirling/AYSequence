#include <AYTest.h>
#include <AYSequence/SequenceTextPresenter.h>
#include <AYLocalization.h>
#include <AYUI/TextLabel.h>
#include <array>

using namespace ayt::sequence;
namespace {
Sequence captions() {
    Sequence s; s.id="captions"; s.duration=3;
    s.textCues={{"line-a","line.a","speaker.a",0,1.5},
                {"line-b","line.b","",1,2.5}};
    return s;
}
}
TEST_SUITE(SequenceTextPresenterTests)
TEST_CASE(seek_overlap_and_language_refresh_update_owned_ui_labels) {
    auto sequence=CompiledSequence::compile(captions());
    CHECK(sequence!=nullptr);
    auto& locale=ayt::localization::Localization::instance();
    locale.clear(); locale.addSupportedLanguage("en-US"); locale.setLanguage("en-US");
    locale.setText("line.a","Hello"); locale.setText("speaker.a","Guide");
    locale.setText("line.b","Goodbye");
    ayt::ui::TextLabel s1,b1,s2,b2;
    const std::array slots{SequenceTextSlot{&s1,&b1},SequenceTextSlot{&s2,&b2}};
    SequenceTextPresenter presenter;
    CHECK(presenter.bind(slots));
    CHECK(presenter.seek(*sequence,0.5,locale));
    CHECK(b1.isVisible() && b1.getText()==L"Hello");
    CHECK(s1.isVisible() && s1.getText()==L"Guide");
    CHECK_FALSE(b2.isVisible());
    CHECK(presenter.seek(*sequence,1.2,locale));
    CHECK(b2.isVisible() && b2.getText()==L"Goodbye");
    locale.setText("line.a","Welcome");
    CHECK(presenter.seek(*sequence,1.2,locale));
    CHECK(b1.getText()==L"Welcome");
    CHECK_FALSE(s2.isVisible());
    CHECK(presenter.seek(*sequence,1.5,locale));
    CHECK(b1.isVisible() && b1.getText()==L"Goodbye");
    CHECK_FALSE(b2.isVisible());
    CHECK_FALSE(presenter.seek(*sequence,4,locale));
    CHECK(b1.isVisible());
    CHECK(presenter.seek(*sequence,2.5,locale));
    CHECK_FALSE(b1.isVisible());
    locale.clear();
}
TEST_CASE(capacity_and_invalid_binding_preserve_visible_state) {
    auto sequence=CompiledSequence::compile(captions());
    auto& locale=ayt::localization::Localization::instance();
    locale.clear();
    ayt::ui::TextLabel speaker,body;
    SequenceTextPresenter presenter;
    const std::array one{SequenceTextSlot{&speaker,&body}};
    CHECK(presenter.bind(one));
    CHECK(presenter.seek(*sequence,0.5,locale));
    CHECK(body.getText()==L"line.a");
    CHECK_FALSE(presenter.seek(*sequence,1.2,locale));
    CHECK(body.getText()==L"line.a" && body.isVisible());
    const std::array invalid{SequenceTextSlot{&speaker,&speaker}};
    CHECK_FALSE(presenter.bind(invalid));
    CHECK_INT_EQ(presenter.slotCount(),1);
    presenter.clear();
    CHECK_FALSE(body.isVisible());
}
TEST_SUITE_END
