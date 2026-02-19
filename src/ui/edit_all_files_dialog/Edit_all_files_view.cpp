#include "ui/edit_all_files_dialog/Edit_all_files_view.h"
#include "ui/Gui_util.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QVBoxLayout>

Edit_all_files_view::Edit_all_files_view(QWidget* parent)
    : QDialog(parent),
      m_tag_path_edit(new QLineEdit()),
      m_value_edit(new QPlainTextEdit()),
      m_set_button(new QRadioButton("Set element")),
      m_set_existing_button(new QRadioButton("Set existing element")),
      m_delete_button(new QRadioButton("Delete element")) {
    auto layout = new QVBoxLayout(this);

    auto tag_path_label = new QLabel("Tag path [?](.)");
    tag_path_label->setTextFormat(Qt::MarkdownText);
    connect(tag_path_label, &QLabel::linkActivated, [this] {show_tag_path_help();});
    layout->addWidget(tag_path_label);

    m_tag_path_edit->setPlaceholderText("E.g. PatientName or 10,10");
    layout->addWidget(m_tag_path_edit);
	
	connect(m_tag_path_edit, &QLineEdit::textChanged, [this](const QString& text) {
    const QString lowered = text.trimmed().toLower();
    const bool is_study_date = lowered.contains("0008,0020") || lowered.contains("studydate");

    // Disable value entry and show clear messaging
    m_value_edit->setEnabled(!is_study_date);
    if (is_study_date) {
        m_value_edit->setPlainText({});
        m_value_edit->setPlaceholderText("Editing StudyDate (0008,0020) is disabled.");
    } else {
        m_value_edit->setPlaceholderText("Enter value. If VM > 1, separate values with '\\'.");
    }

    // Visually “remove” the option by disabling edit/delete modes
    m_set_button->setEnabled(!is_study_date);
    m_set_existing_button->setEnabled(!is_study_date);
    m_delete_button->setEnabled(!is_study_date);

    // If user was on a now-disabled mode, move them back to a neutral mode
    if (is_study_date) {
        // Choose a benign default: keep "Set element" selected but disabled,
        // or uncheck all (Qt RadioButtons prefer one checked; leaving as-is is fine
        // because apply() refuses anyway). No-op here is acceptable.
    }
	});

    m_set_button->setChecked(true);
    connect(m_set_button, &QRadioButton::toggled, [this] {mode_changed();});
    layout->addWidget(m_set_button);

    connect(m_set_existing_button, &QRadioButton::toggled, [this] {mode_changed();});
    layout->addWidget(m_set_existing_button);

    connect(m_delete_button, &QRadioButton::toggled, [this] {mode_changed();});
    layout->addWidget(m_delete_button);

    m_value_edit->setPlaceholderText("Enter value. If VM > 1, separate values with '\\'.");
    m_value_edit->setTabChangesFocus(true);
    layout->addWidget(m_value_edit);

    auto button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(button_box, &QDialogButtonBox::accepted, [this] {ok_clicked();});
    connect(button_box, &QDialogButtonBox::rejected, [this] {cancel_clicked();});
    layout->addWidget(button_box);
    setWindowTitle("Edit all files");
}

void Edit_all_files_view::show_dialog() {
    exec();
}

void Edit_all_files_view::close_dialog() {
    accept();
}

void Edit_all_files_view::show_error(const std::string& title, const std::string& text) {
    QMessageBox::critical(this, QString::fromStdString(title), QString::fromStdString(text));
}

void Edit_all_files_view::show_error_details(const std::vector<std::string>& error_list) {
    QMessageBox dialog(QMessageBox::Critical, "Error", "At least one operation failed.",
        QMessageBox::Ok, this);

    QString details;
    for(const std::string& error : error_list) {
        details += QString::fromStdString(error) + "\n\n";
    }
    dialog.setDetailedText(details);
    dialog.exec();
}

void Edit_all_files_view::enable_value(bool enabled) {
    m_value_edit->setEnabled(enabled);
}

std::string Edit_all_files_view::tag_path() {
    return m_tag_path_edit->text().toStdString();
}

std::string Edit_all_files_view::value() {
    return m_value_edit->toPlainText().toStdString();
}

Edit_all_files_view::Mode Edit_all_files_view::mode() {
    if(m_set_button->isChecked()) {
        return Mode::set;
    }
    else if(m_set_existing_button->isChecked()) {
        return Mode::set_existing;
    }
    else {
        return Mode::remove;
    }
}

void Edit_all_files_view::show_tag_path_help() {
    QMessageBox::information(this, "Tag path", Gui_util::get_tag_path_help());
}
