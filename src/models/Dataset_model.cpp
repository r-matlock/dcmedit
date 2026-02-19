#include "models/Dataset_model.h"

#include "common/Dicom_util.h"
#include "logging/Log.h"

#include <array>
#include <cstdint>
#include <dcmtk/dcmdata/dcelem.h>
#include <dcmtk/dcmdata/dcistrmf.h>
#include <dcmtk/dcmdata/dcitem.h>
#include <dcmtk/dcmdata/dcsequen.h>
#include <dcmtk/dcmdata/dctagkey.h>
#include <stdexcept>
#include <QBrush>      // ← ADD
#include <QFont>       // ← ADD
#include <QColor>      // ← ADD

const std::array<const char*, 4> columns = {"Tag", "VR", "Length", "Value"};
const int max_value_display_length = 100;

// ---------------------------------------------------------------------------
// Helper: only allow edits for three specific tags
// ---------------------------------------------------------------------------

namespace
{
    bool is_allowed_edit_tag(DcmElement* element)
    {
        if (!element) {
            return false;
        }

        const DcmTagKey key = element->getTag().getXTag();

        // (0010,0010) PatientName
        if (key == DcmTagKey(0x0010, 0x0010)) {
            return true;
        }

        // (0010,0020) PatientID
        if (key == DcmTagKey(0x0010, 0x0020)) {
            return true;
        }

        // (0020,000D) StudyInstanceUID
        if (key == DcmTagKey(0x0020, 0x000D)) {
            return true;
        }

        return false;
    }
}

// ---------------------------------------------------------------------------

static QVariant get_item_data(DcmItem& item, int row, int column) {
    QVariant data;

    switch(column) {
        case 0:
            data = QVariant("Item " + QString::number(row + 1));
            break;
        case 2:
            data = QVariant(item.getLength());
            break;
    }
    return data;
}

static QVariant get_element_data(DcmElement& element, int column) {
    QVariant data;
    DcmTag tag = element.getTag();
    auto length = element.getLength();

    switch(column) {
        case 0: {
            QString tag_str = tag.toString().c_str();
            tag_str = tag_str + " " + tag.getTagName();
            data = QVariant(tag_str);
            break;
        }
        case 1:
            data = QVariant(tag.getVRName());
            break;
        case 2:
            data = QVariant(length);
            break;
        case 3: {
            OFString value;
            if(tag.getEVR() != EVR_SQ) {
                if(length <= max_value_display_length) {
                    element.getOFStringArray(value, false);
                }
                else {
                    value = "<Large value, right-click and choose \"Edit\" for more details.>";
                }
            }
            data = QVariant(value.c_str());
            break;
        }
    }
    return data;
}

Dataset_model::Dataset_model(Dicom_files& files)
    : m_files(files) {
    setup_event_callbacks();
}

void Dataset_model::setup_event_callbacks() {
    m_files.current_file_set.add_callback([this] {reset_model();});
    m_files.all_files_edited.add_callback([this] {reset_model();});
}

DcmItem* Dataset_model::get_dataset() const {
    if(Dicom_file* file = m_files.get_current_file()) {
        return &file->get_dataset();
    }
    else {
        Log::debug("Failed to get dataset");
        return nullptr;
    }
}

DcmObject* Dataset_model::get_object(const QModelIndex& index) const {
    if(!index.isValid()) {
        return get_dataset();
    }
    auto object = static_cast<DcmObject*>(index.internalPointer());

    if(!object) {
        Log::error("QModelIndex::internalPointer was null.");
    }
    return object;
}

DcmEVR Dataset_model::get_vr(const QModelIndex& index) const {
    if(!index.isValid()) {
        return EVR_UNKNOWN;
    }
    DcmObject* object = get_object(index);
    return object ? object->ident() : EVR_UNKNOWN;
}

void Dataset_model::add_element(const QModelIndex& index, const std::string& tag_path, const std::string& value) {
    DcmObject* object = get_object(index);

    if(object == nullptr) {
        throw std::runtime_error("failed to get object");
    }
    layoutAboutToBeChanged({QPersistentModelIndex(index)});
    Dicom_util::set_element(tag_path, value, true, *object);
    layoutChanged({QPersistentModelIndex(index)});
    mark_as_modified();
}

void Dataset_model::add_item(const QModelIndex& index) {
    auto sq = dynamic_cast<DcmSequenceOfItems*>(get_object(index));

    if(sq == nullptr) {
        throw std::runtime_error("failed to get sequence");
    }
    int item_pos = rowCount(index);
    beginInsertRows(index, item_pos, item_pos);
    OFCondition status = sq->append(new DcmItem());
    endInsertRows();
    mark_as_modified();

    if(status.bad()) {
        throw std::runtime_error(status.text());
    }
}

void Dataset_model::delete_index(const QModelIndex& index) {
    if(!index.isValid()) {
        throw std::runtime_error("invalid index");
    }
    DcmObject* parent = get_object(index.parent());

    if(parent == nullptr) {
        throw std::runtime_error("failed to get parent");
    }
    const DcmEVR vr = parent->ident();
    const int row = index.row();
    bool bad_vr = false;
    beginRemoveRows(index.parent(), row, row);

    if(vr == EVR_item || vr == EVR_dataset) {
        auto item = static_cast<DcmItem*>(parent);
        DcmElement* element = item->remove(row);
        delete element;
    }
    else if(vr == EVR_SQ) {
        auto sq = static_cast<DcmSequenceOfItems*>(parent);
        DcmItem* item = sq->remove(row);
        delete item;
    }
    else {
        bad_vr = true;
    }
    endRemoveRows();
    mark_as_modified();

    if(bad_vr) {
        throw std::runtime_error("Unexpected VR: " + std::to_string(vr));
    }
}

// ---------------------------------------------------------------------------
// Only allow edits on the three whitelisted tags
// ---------------------------------------------------------------------------

void Dataset_model::set_value(const QModelIndex& index, const std::string& value) {
    auto element = dynamic_cast<DcmElement*>(get_object(index));

    if(element == nullptr) {
        throw std::runtime_error("failed to get element");
    }

    if (!is_allowed_edit_tag(element)) {
        Log::info("Ignoring edit to non-whitelisted tag.");
        return;
    }

    OFCondition status = element->putString(value.c_str());
    dataChanged(index, index);
    mark_as_modified();

    if(status.bad()) {
        throw std::runtime_error(status.text());
    }
}

void Dataset_model::set_value_from_file(const QModelIndex& index, const std::string& file_path) {
    auto element = dynamic_cast<DcmElement*>(get_object(index));

    if(element == nullptr) {
        throw std::runtime_error("failed to get element");
    }

    if (!is_allowed_edit_tag(element)) {
        Log::info("Ignoring file-based edit to non-whitelisted tag.");
        return;
    }

    DcmInputFileStream file_stream(file_path.c_str());
    const auto file_size = static_cast<uint32_t>(OFStandard::getFileSize(file_path.c_str()));

    if(file_size % 2) {
        throw std::runtime_error("file size must be even");
    }
    OFCondition status = element->createValueFromTempFile(file_stream.newFactory(), file_size, EBO_LittleEndian);
    dataChanged(index, index);
    mark_as_modified();

    if(status.bad()) {
        throw std::runtime_error(status.text());
    }
}

// ---------------------------------------------------------------------------

QModelIndex Dataset_model::index(int row, int column, const QModelIndex& parent) const 
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    DcmObject* parentObj = get_object(parent);
    if (!parentObj)
        return QModelIndex();

    int total = parentObj->getNumberOfValues();
    int visibleIndex = -1;

    for (int i = 0; i < total; ++i) {
        DcmObject* child = nullptr;

        if (parentObj->ident() == EVR_item || parentObj->ident() == EVR_dataset) {
            child = static_cast<DcmItem*>(parentObj)->getElement(i);
        } 
        else if (parentObj->ident() == EVR_SQ) {
            child = static_cast<DcmSequenceOfItems*>(parentObj)->getItem(i);
        }

        DcmElement* elem = dynamic_cast<DcmElement*>(child);
        if (elem && is_allowed_edit_tag(elem)) {
            ++visibleIndex;

            if (visibleIndex == row) {
                return createIndex(row, column, child);
            }
        }
    }

    return QModelIndex();
}


QModelIndex Dataset_model::parent(const QModelIndex& index) const {
    if(!index.isValid()) {
        return QModelIndex();
    }
    DcmObject* object = get_object(index);
    DcmObject* parent = object ? object->getParent() : nullptr;

    if(!parent || parent == get_dataset()) {
        return QModelIndex();
    }
    int row = Dicom_util::get_index_nr(*parent);
    return createIndex(row, 0, parent);
}

int Dataset_model::rowCount(const QModelIndex& parent) const {
    DcmObject* object = get_object(parent);

    if (!object || object->isLeaf())
    return 0;

	int total = object->getNumberOfValues();
	int count = 0;

	for (int i = 0; i < total; ++i) {
		DcmObject* child = nullptr;

		if (object->ident() == EVR_item || object->ident() == EVR_dataset) {
			child = static_cast<DcmItem*>(object)->getElement(i);
		} else if (object->ident() == EVR_SQ) {
			child = static_cast<DcmSequenceOfItems*>(object)->getItem(i);
		}

		DcmElement* elem = dynamic_cast<DcmElement*>(child);
		if (elem && is_allowed_edit_tag(elem))
    count++;
}

return count;
}

int Dataset_model::columnCount(const QModelIndex&) const {
    return static_cast<int>(columns.size());
}

Qt::ItemFlags Dataset_model::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    DcmObject* object = get_object(index);
    DcmElement* element = dynamic_cast<DcmElement*>(object);

    // Only allow editing the Value column of the three specific tags
    if (element && index.column() == 3 && is_allowed_edit_tag(element)) {
        flags |= Qt::ItemIsEditable;
    }

    return flags;
}

QVariant Dataset_model::data(const QModelIndex& index, int role) const {
    DcmObject* object = get_object(index);
    if (!index.isValid() || !object) {
        return QVariant();
    }

    // Determine if this row corresponds to one of the allowed tags
    DcmElement* element = dynamic_cast<DcmElement*>(object);
    const bool row_is_editable_tag = is_allowed_edit_tag(element);

    // --- Display text (unchanged logic) ---
    if (role == Qt::DisplayRole) {
        QVariant data;
        const DcmEVR vr = object->ident();

        if (vr == EVR_item || vr == EVR_dataset) {
            auto item = static_cast<DcmItem*>(object);
            data = get_item_data(*item, index.row(), index.column());
        }
        else {
            auto elem = static_cast<DcmElement*>(object);
            data = get_element_data(*elem, index.column());
        }
        return data;
    }

    // --- Grey out everything except those three tags ---
	if (role == Qt::ForegroundRole) {
		if (row_is_editable_tag) {
			// FORCE true black text
			return QBrush(QColor(Qt::black));
		} else {
			// Everything else stays light grey
			return QBrush(QColor(Qt::gray));
    }
}

    return QVariant();
}

bool Dataset_model::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid())
        return false;

    if (role != Qt::EditRole)
        return false;

    DcmObject* object = get_object(index);
    DcmElement* element = dynamic_cast<DcmElement*>(object);
    if (!element)
        return false;

    // Only allow the three specific editable tags
    if (!is_allowed_edit_tag(element))
        return false;

    // Convert QVariant → std::string
    std::string newValue = value.toString().toStdString();

    try {
        // Use your existing setter to write to the DICOM element
        set_value(index, newValue);

        // Notify views
        emit dataChanged(index, index);
        return true;
    }
    catch (...) {
        return false;
    }
}

QVariant Dataset_model::headerData(int section, Qt::Orientation orientation, int role) const {
    if(orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    return QVariant(columns[section]);
}

void Dataset_model::reset_model() {
    beginResetModel();
    endResetModel();
    dataset_changed();
    Log::debug("Dataset model was reset");
}

void Dataset_model::mark_as_modified() {
    m_files.get_current_file()->set_unsaved_changes(true);
    dataset_changed();
}
