// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Caio Venâncio <caio.venancio784@gmail.com>          *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include <algorithm>
#include <cmath>

#include <QWidget>
#include <QAbstractItemView>
#include <QEvent>
#include <QLineEdit>
#include <QVariant>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Application.h>
#include <Gui/Selection/Selection.h>
#include <Gui/ViewProvider.h>
#include <Mod/PartDesign/Gui/ReferenceSelection.h>
#include <Mod/PartDesign/App/FeatureThread.h>

#include "ui_TaskThreadParameters.h"
#include "TaskThreadParameters.h"
#include "TaskDressUpParameters.h"

using namespace PartDesignGui;
using namespace Gui;
namespace sp = std::placeholders;

TaskThreadParameters::TaskThreadParameters(ViewProviderDressUp* threadView, QWidget* parent)
    : TaskDressUpParameters(threadView, true, true, parent)
    , ui(new Ui_TaskThreadParameters)
    , observer(new Observer(this, getObject<PartDesign::Thread>()))
{
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    this->groupLayout()->addWidget(proxy);

    QMetaObject::connectSlotsByName(this);

    const PartDesign::Thread* thread = threadView->getObject<PartDesign::Thread>();

    setLinkSubText(ui->lateralFaceEdit, thread->LateralFace);
    setLinkSubText(ui->startEdit, thread->StartPlane);
    setLinkSubText(ui->upToGeometryEdit, thread->UpToGeometry);

    const std::vector<std::string> strings = thread->Base.getSubValues();

    setThreadSelectionMode(SideFaceSel);

    ui->endTypeCombo->setCurrentIndex(thread->DepthType.getValue());
    const bool isDimension = std::string(thread->DepthType.getValueAsString()) == "Dimension";
    const bool isUpToGeometry = std::string(thread->DepthType.getValueAsString()) == "UpToGeometry";

    ui->labelDepth->setHidden(!isDimension);
    ui->Depth->setHidden(!isDimension);
    ui->upToGeometryWidget->setHidden(!isUpToGeometry);
    ui->Depth->setValue(thread->Depth.getValue());

    const auto addComboBoxItems = [this](QComboBox* widget, const auto& values) {
        for (const auto& value : values) {
            widget->addItem(tr(value.c_str()));
        }
    };

    ui->standardCombo->clear();
    populateStandardCombo();

    populateSizeCombo();

    ui->classCombo->clear();
    const std::vector<std::string> cursorClass = thread->ThreadClass.getEnumVector();
    addComboBoxItems(ui->classCombo, cursorClass);
    ui->classCombo->setCurrentIndex(thread->ThreadClass.getValue());

    populateSeriesCombo();

    ui->pitchCombo->clear();
    const std::vector<std::string> cursorPitch = thread->ThreadSizePitch.getEnumVector();
    addComboBoxItems(ui->pitchCombo, cursorPitch);
    ui->pitchCombo->setCurrentIndex(thread->ThreadSizePitch.getValue());

    ui->customPitchField->setRange(0.001, 1000.0);
    updateCustomFields();

    ui->directionCombo->clear();
    const std::vector<std::string> cursorDirection = thread->ThreadDirection.getEnumVector();
    addComboBoxItems(ui->directionCombo, cursorDirection);
    ui->directionCombo->setCurrentIndex(thread->ThreadDirection.getValue());


    ui->customClearanceCheck->setChecked(thread->UseCustomThreadClearance.getValue());
    ui->customClearanceField->setEnabled(ui->customClearanceCheck->isChecked());
    ui->classCombo->setEnabled(!ui->customClearanceCheck->isChecked());
    ui->customClearanceField->setValue(thread->CustomThreadClearance.getValue());

    const bool isModeled = thread->ModelThread.getValue();
    ui->modelledThreadRadio->setChecked(isModeled);

    ui->designationEdit->setReadOnly(true);
    ui->designationEdit->setText(thread->ThreadDesignation.getValue());

    updateFieldVisibility();

    connect(
        ui->standardCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadTypeChanged
    );

    connect(
        ui->styleCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadStyleChanged
    );

    connect(
        ui->rTypeCheck,
        &QCheckBox::toggled,
        this,
        &TaskThreadParameters::threadRTypeChanged
    );

    connect(
        ui->Depth,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        &TaskThreadParameters::depthChanged
    );

    connect(
        ui->diameterCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadSizeChanged
    );

    connect(
        ui->pitchCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadSizePitchChanged
    );

    connect(
        ui->seriesCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadSeriesChanged
    );

    connect(
        ui->customSizeField,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        &TaskThreadParameters::customThreadSizeChanged
    );

    connect(
        ui->customPitchField,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this,
        &TaskThreadParameters::customThreadPitchChanged
    );

    connect(
        ui->classCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadClassChanged
    );

    connect(
        ui->directionCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::threadDirectionChanged
    );

    connect(ui->selectLateralFace, &QPushButton::toggled, [this](bool checked) {
        if (checked) {
            ui->selectLateralFace->setChecked(false);
            setThreadSelectionMode(SideFaceSel);
        }
        else if (currentSelectionMode == SideFaceSel) {
            setThreadSelectionMode(None);
        }
    });

    connect(ui->selectStart, &QPushButton::toggled, [this](bool checked) {
        if (checked) {
            ui->selectStart->setChecked(false);
            setThreadSelectionMode(StartFaceSel);
        }
        else if (currentSelectionMode == StartFaceSel) {
            setThreadSelectionMode(None);
        }
    });

    connect(ui->selectUpToGeometry, &QPushButton::toggled, [this](bool checked) {
        if (checked) {
            ui->selectUpToGeometry->setChecked(false);
            setThreadSelectionMode(UpToGeometrySel);
        }
        else if (currentSelectionMode == UpToGeometrySel) {
            setThreadSelectionMode(None);
        }
    });


    connect(
        ui->endTypeCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        &TaskThreadParameters::depthTypeChanged
    );

    connect(
        ui->customClearanceCheck,
        &QCheckBox::toggled,
        this,
        &TaskThreadParameters::useCustomThreadClearanceChanged
    );

    connect(
        ui->customClearanceField,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        &TaskThreadParameters::customThreadClearanceChanged
    );

    connect(
        ui->cosmeticThreadRadio,
        &QRadioButton::clicked,
        this,
        &TaskThreadParameters::threadModelChanged
    );

    connect(
        ui->modelledThreadRadio,
        &QRadioButton::clicked,
        this,
        &TaskThreadParameters::threadModelChanged
    );

    // NOLINTBEGIN
    connectPropChanged = App::GetApplication().signalChangePropertyEditor.connect(
        std::bind(&TaskThreadParameters::changedObject, this, sp::_1, sp::_2)
    );
    // NOLINTEND

    if (strings.size() == 0) {
        setThreadSelectionGate();
    }
    else {
        hideOnError();
    }
}

TaskThreadParameters::~TaskThreadParameters()
{
    try {
        Gui::Selection().clearSelection();
        Gui::Selection().rmvSelectionGate();
    }
    catch (const std::exception&) {
    }
}

void TaskThreadParameters::changeEvent(QEvent* e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
    }
}

bool TaskThreadParameters::getModelThread() const
{
    return ui->modelledThreadRadio->isChecked();
}

bool TaskThreadParameters::getCosmeticThreaded() const
{
    return ui->cosmeticThreadRadio->isChecked();
}

void TaskThreadParameters::apply()
{
    auto thread = getObject<PartDesign::Thread>();
    if (thread->LateralFace.getSubValues().empty()) {
        Base::Console().warning(tr("Empty thread created!\n").toStdString().c_str());
    }
}

void TaskThreadParameters::onRefDeleted()
{}

void TaskThreadParameters::setButtons(const PartDesignGui::TaskDressUpParameters::selectionModes mode)
{
    Q_UNUSED(mode);
}

void TaskThreadParameters::setThreadSelectionMode(threadSelectionModes mode)
{
    currentSelectionMode = mode;
}

void TaskThreadParameters::setLinkSubText(QLineEdit* edit, const App::PropertyLinkSub& prop)
{
    const auto& subs = prop.getSubValues();

    if (!subs.empty() && !subs.front().empty()) {
        edit->setText(QString::fromStdString(subs.front()));
    }
    else if (auto obj = prop.getValue()) {
        edit->setText(QString::fromUtf8(obj->Label.getValue()));
    }
    else {
        edit->setText(QString::fromUtf8("No selection"));
    }
}

void TaskThreadParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (msg.Type != Gui::SelectionChanges::AddSelection) {
        return;
    }

    switch (currentSelectionMode) {
        case SideFaceSel: {
            auto thread = getObject<PartDesign::Thread>();
            std::vector<std::string> planes;
            App::DocumentObject* selectedObject = nullptr;
            getReferencedSelection(thread, msg, selectedObject, planes);
            if (!selectedObject) {
                return;
            }
            setupTransaction();
            thread->LateralFace.setValue(selectedObject, planes);
            recomputeFeature();
            setLinkSubText(ui->lateralFaceEdit, thread->LateralFace);
            populateStandardCombo();
            updateFieldVisibility();
            ui->selectLateralFace->setChecked(false);
            Gui::Selection().clearSelection();
            break;
        }

        case StartFaceSel: {
            auto thread = getObject<PartDesign::Thread>();
            std::vector<std::string> planes;
            App::DocumentObject* selectedObject = nullptr;
            getReferencedSelection(thread, msg, selectedObject, planes);
            if (!selectedObject) {
                return;
            }
            setupTransaction();
            thread->StartPlane.setValue(selectedObject, planes);
            recomputeFeature();
            setLinkSubText(ui->startEdit, thread->StartPlane);
            ui->selectStart->setChecked(false);
            Gui::Selection().clearSelection();
            break;
        }

        case UpToGeometrySel: {
            auto thread = getObject<PartDesign::Thread>();
            std::vector<std::string> planes;
            App::DocumentObject* selectedObject = nullptr;
            getReferencedSelection(thread, msg, selectedObject, planes);
            if (!selectedObject) {
                return;
            }
            setupTransaction();
            thread->UpToGeometry.setValue(selectedObject, planes);
            recomputeFeature();
            setLinkSubText(ui->upToGeometryEdit, thread->UpToGeometry);
            ui->selectUpToGeometry->setChecked(false);
            Gui::Selection().clearSelection();
            break;
        }

        default:
            break;
    }
}


void TaskThreadParameters::threadTypeChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    const QVariant data = ui->standardCombo->itemData(index);
    const int threadType = data.isValid() ? data.toInt() : index;

    const QString threadClassText = ui->classCombo->currentText();

    setupTransaction();
    thread->ThreadType.setValue(threadType);

    const int threadClassIndex = ui->classCombo->findText(threadClassText, Qt::MatchContains);
    if (threadClassIndex > -1) {
        ui->classCombo->setCurrentIndex(threadClassIndex);
    }

    updateFieldVisibility();

    recomputeFeature();
}

void TaskThreadParameters::threadStyleChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    const QVariant data = ui->styleCombo->itemData(index);
    if (!data.isValid()) {
        return;
    }
    const int threadType = data.toInt();
    if (threadType == thread->ThreadType.getValue()) {
        return;
    }

    setupTransaction();
    thread->ThreadType.setValue(threadType);

    updateFieldVisibility();

    recomputeFeature();
}

void TaskThreadParameters::threadRTypeChanged(bool checked)
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    const int currentType = thread->ThreadType.getValue();
    const int rTypeBase = thread->getRTypeBase(currentType);
    int threadType = -1;
    if (checked && rTypeBase < 0) {
        threadType = thread->getRTypeVariant(currentType);
    }
    else if (!checked && rTypeBase >= 0) {
        threadType = rTypeBase;
    }
    if (threadType < 0 || threadType == currentType) {
        return;
    }

    const double diameter = getCurrentSizeDiameter();
    setupTransaction();
    thread->ThreadType.setValue(threadType);
    const int sizeIndex = findSizeIndex(threadType, diameter);
    if (sizeIndex >= 0 && sizeIndex != thread->ThreadSize.getValue()) {
        thread->ThreadSize.setValue(sizeIndex);
    }

    updateFieldVisibility();

    recomputeFeature();
}

void TaskThreadParameters::threadSizeChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    const QVariant data = ui->diameterCombo->itemData(index);
    setupTransaction();
    if (!data.isValid()) {
        thread->UseCustomThreadSize.setValue(true);
        recomputeFeature();
        return;
    }

    const double diameter = data.toDouble();
    if (thread->UseCustomThreadSize.getValue()) {
        thread->UseCustomThreadSize.setValue(false);
    }

    int threadType = thread->ThreadType.getValue();
    if (findSizeIndex(threadType, diameter) < 0) {
        for (const int member : getSizeGroupMembers()) {
            if (findSizeIndex(member, diameter) >= 0) {
                threadType = member;
                break;
            }
        }
    }
    if (threadType != thread->ThreadType.getValue()) {
        thread->ThreadType.setValue(threadType);
    }

    const int sizeIndex = findSizeIndex(threadType, diameter);
    if (sizeIndex >= 0 && sizeIndex != thread->ThreadSize.getValue()) {
        thread->ThreadSize.setValue(sizeIndex);
    }
    recomputeFeature();
}

void TaskThreadParameters::threadSeriesChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    const QVariant typeData = ui->seriesCombo->itemData(index, Qt::UserRole);
    setupTransaction();
    if (!typeData.isValid()) {
        thread->UseCustomThreadPitch.setValue(true);
        recomputeFeature();
        return;
    }

    const int threadType = typeData.toInt();
    const std::string series
        = ui->seriesCombo->itemData(index, Qt::UserRole + 1).toString().toStdString();
    const double diameter = getCurrentSizeDiameter();

    if (thread->UseCustomThreadPitch.getValue()) {
        thread->UseCustomThreadPitch.setValue(false);
    }
    if (threadType != thread->ThreadType.getValue()) {
        thread->ThreadType.setValue(threadType);
    }
    const int sizeIndex = findSizeIndex(threadType, diameter);
    if (sizeIndex >= 0 && sizeIndex != thread->ThreadSize.getValue()) {
        thread->ThreadSize.setValue(sizeIndex);
    }
    if (thread->ThreadSeries.isPartOf(series.c_str())
        && series != thread->ThreadSeries.getValueAsString()) {
        thread->ThreadSeries.setValue(series.c_str());
    }
    recomputeFeature();
}

void TaskThreadParameters::customThreadSizeChanged(double value)
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread || value <= 0.0) {
        return;
    }
    setupTransaction();
    thread->CustomThreadSize.setValue(value);
    recomputeFeature();
}

void TaskThreadParameters::customThreadPitchChanged(double value)
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread || value <= 0.0) {
        return;
    }
    const double pitch = thread->getPitchUnit() == "tpi" ? 25.4 / value : value;
    setupTransaction();
    thread->CustomThreadPitch.setValue(pitch);
    recomputeFeature();
}

void TaskThreadParameters::threadSizePitchChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto thread = getObject<PartDesign::Thread>();
    if (thread) {
        setupTransaction();
        thread->ThreadSizePitch.setValue(index);
        recomputeFeature();
    }
}

void TaskThreadParameters::depthTypeChanged(int index)
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }
    setupTransaction();
    thread->DepthType.setValue(index);

    const bool isDimension = std::string(thread->DepthType.getValueAsString()) == "Dimension";
    const bool isUpToGeometry = std::string(thread->DepthType.getValueAsString()) == "UpToGeometry";
    ui->labelDepth->setHidden(!isDimension);
    ui->Depth->setHidden(!isDimension);
    ui->upToGeometryWidget->setHidden(!isUpToGeometry);
    recomputeFeature();
}

void TaskThreadParameters::depthChanged(double value)
{
    if (auto thread = getObject<PartDesign::Thread>()) {
        setupTransaction();
        thread->Depth.setValue(value);
        recomputeFeature();
    }
}

void TaskThreadParameters::threadClassChanged(int index)
{
    if (index < 0) {
        return;
    }

    if (auto thread = getObject<PartDesign::Thread>()) {
        setupTransaction();
        thread->ThreadClass.setValue(index);
        recomputeFeature();
    }
}

void TaskThreadParameters::threadDirectionChanged(int index)
{
    if (index < 0) {
        return;
    }

    if (auto thread = getObject<PartDesign::Thread>()) {
        setupTransaction();
        thread->ThreadDirection.setValue(index);
        recomputeFeature();
    }
}

void TaskThreadParameters::useCustomThreadClearanceChanged()
{
    if (auto thread = getObject<PartDesign::Thread>()) {
        setupTransaction();
        thread->UseCustomThreadClearance.setValue(ui->customClearanceCheck->isChecked());
        ui->customClearanceField->setEnabled(ui->customClearanceCheck->isChecked());
        ui->classCombo->setEnabled(!ui->customClearanceCheck->isChecked());
        updateFieldVisibility();

        recomputeFeature();
    }
}

void TaskThreadParameters::threadModelChanged()
{
    if (auto thread = getObject<PartDesign::Thread>()) {
        setupTransaction();
        if (sender() == ui->cosmeticThreadRadio) {
            thread->CosmeticThread.setValue(true);
            thread->ModelThread.setValue(false);
        }
        else {
            thread->CosmeticThread.setValue(false);
            thread->ModelThread.setValue(true);
        }
        recomputeFeature();
    }
}

void TaskThreadParameters::customThreadClearanceChanged(double value)
{
    if (auto thread = getObject<PartDesign::Thread>()) {
        setupTransaction();
        thread->CustomThreadClearance.setValue(value);
        recomputeFeature();
    }
}


void TaskThreadParameters::populateStandardCombo()
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    QSignalBlocker blocker(ui->standardCombo);
    ui->standardCombo->clear();

    const std::vector<std::string> names = thread->ThreadTypeName.getEnumVector();
    const int currentType = thread->ThreadType.getValue();
    const int currentBase = thread->getRTypeBase(currentType);
    int selected = -1;
    int sameGroupItem = -1;
    std::vector<int> groupedTypes;

    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        if (!thread->threadTypeMatchesFace(i)) {
            continue;
        }
        if (std::find(groupedTypes.begin(), groupedTypes.end(), i) != groupedTypes.end()) {
            continue;
        }
        const int rTypeBase = thread->getRTypeBase(i);
        if (rTypeBase >= 0 && thread->threadTypeMatchesFace(rTypeBase)) {
            continue;
        }

        std::vector<int> members = thread->getThreadTypeGroupMembers(i);
        if (members.empty()) {
            members = thread->getThreadSeriesGroupMembers(i);
        }
        if (members.empty()) {
            members.push_back(i);
        }
        groupedTypes.insert(groupedTypes.end(), members.begin(), members.end());

        const auto inMembers = [&members](int type) {
            return type >= 0 && std::find(members.begin(), members.end(), type) != members.end();
        };
        const bool currentInGroup = inMembers(currentType) || inMembers(currentBase);
        int itemType = i;
        if (currentInGroup && thread->threadTypeMatchesFace(currentType)) {
            itemType = currentType;
        }

        const QString itemText = QString::fromStdString(thread->getThreadStandardName(i));
        ui->standardCombo->addItem(itemText, QVariant(itemType));
        ui->standardCombo->setItemData(ui->standardCombo->count() - 1, itemText, Qt::ToolTipRole);
        if (itemType == currentType) {
            selected = ui->standardCombo->count() - 1;
        }
        else if (currentInGroup) {
            sameGroupItem = ui->standardCombo->count() - 1;
        }
    }

    if (selected < 0) {
        selected = sameGroupItem;
    }

    if (selected >= 0) {
        ui->standardCombo->setCurrentIndex(selected);
        const int selectedType = ui->standardCombo->itemData(selected).toInt();
        if (selectedType != currentType) {
            setupTransaction();
            thread->ThreadType.setValue(selectedType);
        }
    }
    else if (ui->standardCombo->count() > 0) {
        ui->standardCombo->setCurrentIndex(0);
        const int fallbackType = ui->standardCombo->itemData(0).toInt();
        if (fallbackType != currentType) {
            setupTransaction();
            thread->ThreadType.setValue(fallbackType);
        }
    }

    populateStyleCombo();
    populateRTypeCheck();
    ui->standardCombo->setToolTip(ui->standardCombo->currentText());
    ui->standardCombo->view()->setMinimumWidth(ui->standardCombo->view()->sizeHintForColumn(0));
}

void TaskThreadParameters::populateRTypeCheck()
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    QSignalBlocker blocker(ui->rTypeCheck);
    const int currentType = thread->ThreadType.getValue();
    const int rTypeBase = thread->getRTypeBase(currentType);
    const int rTypeVariant = rTypeBase >= 0 ? currentType : thread->getRTypeVariant(currentType);
    const bool visible = rTypeVariant >= 0 && thread->threadTypeMatchesFace(rTypeVariant);

    ui->rTypeCheck->setChecked(rTypeBase >= 0);
    ui->rTypeCheck->setVisible(visible);
}

void TaskThreadParameters::populateStyleCombo()
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    QSignalBlocker blocker(ui->styleCombo);
    ui->styleCombo->clear();

    const std::vector<std::string> names = thread->ThreadTypeName.getEnumVector();
    const int currentType = thread->ThreadType.getValue();
    const std::vector<int> members = thread->getThreadTypeGroupMembers(currentType);

    for (const int member : members) {
        if (member < 0 || member >= static_cast<int>(names.size())
            || !thread->threadTypeMatchesFace(member)) {
            continue;
        }
        ui->styleCombo->addItem(tr(names[member].c_str()), QVariant(member));
        if (member == currentType) {
            ui->styleCombo->setCurrentIndex(ui->styleCombo->count() - 1);
        }
    }

    const bool styleVisible = !members.empty();
    ui->labelStyle->setVisible(styleVisible);
    ui->styleCombo->setVisible(styleVisible);
}

std::vector<int> TaskThreadParameters::getSizeGroupMembers() const
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return {};
    }
    const int currentType = thread->ThreadType.getValue();
    std::vector<int> members = thread->getThreadSeriesGroupMembers(currentType);
    if (members.empty()) {
        members.push_back(currentType);
    }
    return members;
}

int TaskThreadParameters::findSizeIndex(int threadType, double diameter) const
{
    constexpr double tolerance = 0.001;
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return -1;
    }
    const std::vector<double> values = thread->getThreadDiameterValues(threadType);
    for (size_t index = 0; index < values.size(); ++index) {
        if (std::abs(values[index] - diameter) < tolerance) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

double TaskThreadParameters::getCurrentSizeDiameter() const
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread || !thread->ThreadSize.isValid()) {
        return 0.0;
    }
    const std::vector<double> values
        = thread->getThreadDiameterValues(thread->ThreadType.getValue());
    const long index = thread->ThreadSize.getValue();
    return index >= 0 && static_cast<size_t>(index) < values.size() ? values[index] : 0.0;
}

void TaskThreadParameters::populateSizeCombo()
{
    constexpr double tolerance = 0.001;
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    QSignalBlocker blocker(ui->diameterCombo);
    ui->diameterCombo->clear();

    const bool customAllowed = thread->allowsCustomSize();
    if (customAllowed) {
        ui->diameterCombo->addItem(tr("Custom"));
    }

    std::vector<std::pair<double, std::string>> sizes;
    for (const int member : getSizeGroupMembers()) {
        const std::vector<double> values = thread->getThreadDiameterValues(member);
        const std::vector<std::string> labels = thread->getThreadDiameterLabels(member);
        for (size_t index = 0; index < values.size() && index < labels.size(); ++index) {
            const bool known = std::any_of(sizes.begin(), sizes.end(), [&](const auto& entry) {
                return std::abs(entry.first - values[index]) < tolerance;
            });
            if (!known) {
                sizes.emplace_back(values[index], labels[index]);
            }
        }
    }
    std::stable_sort(sizes.begin(), sizes.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    const double currentDiameter = getCurrentSizeDiameter();
    int selected = -1;
    for (const auto& [diameter, label] : sizes) {
        ui->diameterCombo->addItem(QString::fromStdString(label), QVariant(diameter));
        if (std::abs(diameter - currentDiameter) < tolerance) {
            selected = ui->diameterCombo->count() - 1;
        }
    }

    if (customAllowed && thread->getCustomThreadValues().size) {
        selected = 0;
    }
    ui->diameterCombo->setCurrentIndex(selected);
}

void TaskThreadParameters::populateSeriesCombo()
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    QSignalBlocker blocker(ui->seriesCombo);
    ui->seriesCombo->clear();

    const int currentType = thread->ThreadType.getValue();
    const std::string currentSeries = thread->ThreadSeries.isValid()
        ? thread->ThreadSeries.getValueAsString()
        : "";
    const double diameter = getCurrentSizeDiameter();
    int selected = -1;

    for (const int member : getSizeGroupMembers()) {
        const int sizeIndex = findSizeIndex(member, diameter);
        if (sizeIndex < 0) {
            continue;
        }
        for (const auto& series : thread->getThreadSeriesForSize(member, sizeIndex)) {
            const QString text = QString::fromStdString(series);
            ui->seriesCombo->addItem(text);
            const int item = ui->seriesCombo->count() - 1;
            ui->seriesCombo->setItemData(item, QVariant(member), Qt::UserRole);
            ui->seriesCombo->setItemData(item, QVariant(text), Qt::UserRole + 1);
            if (member == currentType && series == currentSeries) {
                selected = item;
            }
        }
    }

    if (thread->allowsCustomPitch()) {
        ui->seriesCombo->addItem(tr("Custom"));
        if (thread->getCustomThreadValues().pitch) {
            selected = ui->seriesCombo->count() - 1;
        }
    }
    ui->seriesCombo->setCurrentIndex(selected);
}

void TaskThreadParameters::updateCustomFields()
{
    constexpr int millimeterSchema = 6;
    constexpr int inchSchema = 3;
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    {
        QSignalBlocker blocker(ui->customSizeField);
        ui->customSizeField->setSchema(
            thread->getCustomSizeUnit() == "in" ? inchSchema : millimeterSchema
        );
        ui->customSizeField->setValue(thread->CustomThreadSize.getValue());
    }

    {
        QSignalBlocker blocker(ui->customPitchField);
        const bool threadsPerInch = thread->getPitchUnit() == "tpi";
        const double pitch = thread->CustomThreadPitch.getValue();
        ui->customPitchField->setSuffix(threadsPerInch ? tr(" TPI") : QStringLiteral(" mm"));
        ui->customPitchField->setDecimals(threadsPerInch ? 3 : 4);
        ui->customPitchField->setValue(threadsPerInch && pitch > 0.0 ? 25.4 / pitch : pitch);
    }
}

void TaskThreadParameters::updateFieldVisibility()
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }

    const auto custom = thread->getCustomThreadValues();
    const bool customSize = custom.size.has_value();
    const bool customPitch = custom.pitch.has_value();
    const bool seriesVisible = ui->seriesCombo->count() > 0 && !customSize;
    const bool pitchVisible = thread->isPitchSelectable() && !customPitch;
    const bool classVisible = thread->hasThreadClasses();
    const bool directionVisible = thread->allowsLeftHand();

    ui->customSizeField->setVisible(customSize);

    ui->labelSeries->setVisible(seriesVisible);
    ui->seriesCombo->setVisible(seriesVisible);

    ui->labelCustomPitch->setVisible(customPitch);
    ui->customPitchField->setVisible(customPitch);

    ui->labelPitch->setVisible(pitchVisible);
    ui->pitchCombo->setVisible(pitchVisible);

    ui->labelClass->setVisible(classVisible);
    ui->classCombo->setVisible(classVisible);
    ui->customClearanceCheck->setVisible(classVisible);
    ui->customClearanceField->setVisible(classVisible && ui->customClearanceCheck->isChecked());

    ui->labelDirection->setVisible(directionVisible);
    ui->directionCombo->setVisible(directionVisible);
}

void TaskThreadParameters::changedObject(const App::Document&, const App::Property& property)
{
    auto thread = getObject<PartDesign::Thread>();
    if (!thread) {
        return;
    }
    const bool readOnly = property.isReadOnly();

    const auto updateRadio = [&](QRadioButton* widget, bool value) {
        [[maybe_unused]] QSignalBlocker blocker(widget);
        widget->setChecked(value);
        widget->setDisabled(readOnly);
    };

    const auto updateComboBox = [&](QComboBox* widget, int value) {
        [[maybe_unused]] QSignalBlocker blocker(widget);
        widget->setCurrentIndex(value);
        widget->setDisabled(readOnly);
    };

    const auto updateSpinBox = [&](Gui::PrefQuantitySpinBox* widget, double value) {
        [[maybe_unused]] QSignalBlocker blocker(widget);
        widget->setValue(value);
        widget->setDisabled(readOnly);
    };

    const auto updateComboBoxItems = [](QComboBox* widget, const auto& values, int selected) {
        QSignalBlocker blocker(widget);
        widget->clear();
        for (const auto& value : values) {
            widget->addItem(QString::fromStdString(value));
        }
        widget->setCurrentIndex(selected);
    };

    if (&property == &thread->ThreadType) {
        ui->standardCombo->setEnabled(true);
        populateStandardCombo();
        populateSizeCombo();
        populateSeriesCombo();
        updateComboBoxItems(
            ui->pitchCombo,
            thread->ThreadSizePitch.getEnumVector(),
            thread->ThreadSizePitch.getValue()
        );
        updateCustomFields();

        std::vector<std::string> translatedClassTypes;
        for (const auto& it : thread->ThreadClass.getEnumVector()) {
            translatedClassTypes.push_back(tr(it.c_str()).toStdString());
        }
        updateComboBoxItems(ui->classCombo, translatedClassTypes, thread->ThreadClass.getValue());

        ui->designationEdit->setText(thread->ThreadDesignation.getValue());
        updateFieldVisibility();
    }
    else if (&property == &thread->ThreadDirection) {
        QSignalBlocker blocker(ui->directionCombo);
        ui->directionCombo->setCurrentIndex(thread->ThreadDirection.getValue());
    }
    else if (&property == &thread->UseCustomThreadClearance) {
        QSignalBlocker blocker(ui->customClearanceCheck);
        const bool checked = thread->UseCustomThreadClearance.getValue();
        ui->customClearanceCheck->setChecked(checked);
        ui->customClearanceField->setEnabled(checked);
        ui->classCombo->setEnabled(!checked);
        updateFieldVisibility();
    }
    else if (&property == &thread->CustomThreadClearance) {
        QSignalBlocker blocker(ui->customClearanceField);
        ui->customClearanceField->setValue(thread->CustomThreadClearance.getValue());
    }
    else if (&property == &thread->ThreadClass) {
        QSignalBlocker blocker(ui->classCombo);
        ui->classCombo->clear();
        for (const auto& value : thread->ThreadClass.getEnumVector()) {
            ui->classCombo->addItem(tr(value.c_str()));
        }
        ui->classCombo->setCurrentIndex(thread->ThreadClass.getValue());
    }
    else if (&property == &thread->ThreadSize) {
        populateSizeCombo();
        populateSeriesCombo();
        updateComboBoxItems(
            ui->pitchCombo,
            thread->ThreadSizePitch.getEnumVector(),
            thread->ThreadSizePitch.getValue()
        );

        ui->designationEdit->setText(thread->ThreadDesignation.getValue());
        updateFieldVisibility();
    }
    else if (&property == &thread->ThreadSeries) {
        populateSeriesCombo();
        updateComboBoxItems(
            ui->pitchCombo,
            thread->ThreadSizePitch.getEnumVector(),
            thread->ThreadSizePitch.getValue()
        );
        updateFieldVisibility();
    }
    else if (&property == &thread->UseCustomThreadPitch || &property == &thread->UseCustomThreadSize
             || &property == &thread->CustomThreadPitch || &property == &thread->CustomThreadSize) {
        populateSizeCombo();
        populateSeriesCombo();
        updateCustomFields();
        ui->designationEdit->setText(thread->ThreadDesignation.getValue());
        updateFieldVisibility();
    }
    else if (&property == &thread->ThreadSizePitch) {
        updateComboBox(ui->pitchCombo, thread->ThreadSizePitch.getValue());
        ui->designationEdit->setText(thread->ThreadDesignation.getValue());
    }
    else if (&property == &thread->ThreadDesignation) {
        ui->designationEdit->setText(thread->ThreadDesignation.getValue());
    }
    else if (&property == &thread->LateralFace) {
        setLinkSubText(ui->lateralFaceEdit, thread->LateralFace);
        populateStandardCombo();
        populateSizeCombo();
        populateSeriesCombo();
        updateCustomFields();
        updateFieldVisibility();
    }
    else if (&property == &thread->StartPlane) {
        setLinkSubText(ui->startEdit, thread->StartPlane);
    }
    else if (&property == &thread->UpToGeometry) {
        setLinkSubText(ui->upToGeometryEdit, thread->UpToGeometry);
    }
    else if (&property == &thread->Depth) {
        updateSpinBox(ui->Depth, thread->Depth.getValue());
    }
    else if (&property == &thread->ModelThread || &property == &thread->CosmeticThread) {
        updateRadio(ui->modelledThreadRadio, thread->ModelThread.getValue());
        updateRadio(ui->cosmeticThreadRadio, !thread->ModelThread.getValue());
    }
    else if (&property == &thread->DepthType) {
        ui->endTypeCombo->setEnabled(true);
        updateComboBox(ui->endTypeCombo, thread->DepthType.getValue());
        ui->labelDepth->setHidden(std::string(thread->DepthType.getValueAsString()) != "Dimension");
        ui->Depth->setHidden(std::string(thread->DepthType.getValueAsString()) != "Dimension");
        ui->upToGeometryWidget->setHidden(
            std::string(thread->DepthType.getValueAsString()) != "UpToGeometry"
        );
    }
}

void TaskThreadParameters::setThreadSelectionGate()
{
    if (selectionMode == none) {
        Gui::Selection().rmvSelectionGate();
        return;
    }

    AllowSelectionFlags allow;
    allow.setFlag(AllowSelection::EDGE, allowEdges);
    allow.setFlag(AllowSelection::FACE, allowFaces);
    allow.setFlag(AllowSelection::POINT, true);

    Gui::Selection().addSelectionGate(new ReferenceSelection(this->getBase(), allow));
}


TaskDlgThreadParameters::TaskDlgThreadParameters(ViewProviderThread* threadView)
    : TaskDlgDressUpParameters(threadView)
{
    parameter = new TaskThreadParameters(threadView);

    Content.push_back(parameter);

    Content.push_back(preview);
}

TaskDlgThreadParameters::~TaskDlgThreadParameters() = default;

bool TaskDlgThreadParameters::accept()
{
    parameter->apply();
    return TaskDlgDressUpParameters::accept();
}

bool TaskDlgThreadParameters::reject()
{
    auto* thread = getObject<PartDesign::Thread>();
    App::Document* document = thread ? thread->getDocument() : nullptr;
    const std::string threadName = thread && thread->getNameInDocument()
        ? thread->getNameInDocument()
        : "";

    if (auto* threadView = getViewObject<ViewProviderThread>()) {
        threadView->restoreBaseVisibility();
        threadView->clearThreadTextures();
    }

    const bool result = TaskDlgDressUpParameters::reject();

    auto* restoredThread = document && !threadName.empty()
        ? dynamic_cast<PartDesign::Thread*>(document->getObject(threadName.c_str()))
        : nullptr;
    if (restoredThread) {
        if (auto* restoredView = dynamic_cast<ViewProviderThread*>(
                Gui::Application::Instance->getViewProvider(restoredThread)
            )) {
            restoredView->updateOverlay();
        }
    }

    return result;
}

TaskThreadParameters::Observer::Observer(
    TaskThreadParameters* taskPanel,
    const PartDesign::Thread* threadObject
)
    : DocumentObserver(threadObject->getDocument())
    , owner(taskPanel)
    , thread(threadObject)
{}

void TaskThreadParameters::Observer::slotChangedObject(
    const App::DocumentObject& object,
    const App::Property& property
)
{
    if (&object == thread) {
        if (object.getDocument()) {
            owner->changedObject(*object.getDocument(), property);
        }
    }
}

#include "moc_TaskThreadParameters.cpp"
