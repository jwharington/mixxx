#include "widget/wbeatspinbox.h"

#include <algorithm>

#include <QDomElement>
#include <QKeyEvent>
#include <QRegularExpression>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "moc_wbeatspinbox.cpp"
#include "skin/legacy/skincontext.h"
#include "util/math.h"

namespace {
const QRegularExpression kBlockListRegex(QStringLiteral("[^0-9.,/ ]"));
} // namespace

WBeatSpinBox::WBeatSpinBox(QWidget* parent,
        const ConfigKey& configKey,
        int decimals,
        double minimum,
        double maximum)
        : QDoubleSpinBox(parent),
          WBaseWidget(this),
          m_valueControl(configKey, this, ControlFlag::NoAssertIfMissing),
          m_scaleFactor(1.0),
          m_stepMode(StepMode::PowerOfTwo),
          m_stepSize(1.0),
          m_showButtons(true),
          m_allowStepping(true) {
    // replace the original QLineEdit by one that supports font scaling.
    setLineEdit(new WBeatLineEdit(this));
    setDecimals(decimals);
    setMinimum(minimum);
    setMaximum(maximum);
    setKeyboardTracking(false);
    // Prevent this widget from getting focused with Tab
    // to avoid interfering with using the library via keyboard.
    setFocusPolicy(Qt::ClickFocus);
    // This is necessary to also ignore Shift+Tab (Qt::BacktabFocusReason).
    lineEdit()->setFocusPolicy(Qt::ClickFocus);

    setValue(m_valueControl.get());
    connect(this,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &WBeatSpinBox::slotSpinboxValueChanged);
    m_valueControl.connectValueChanged(this, &WBeatSpinBox::slotControlValueChanged);
}

void WBeatSpinBox::setup(const QDomNode& node, const SkinContext& context) {
    m_scaleFactor = context.getScaleFactor();
    qobject_cast<WBeatLineEdit*>(lineEdit())->setScaleFactor(m_scaleFactor);

    const auto element = node.toElement();

    auto parseBool = [](const QString& text, bool defaultValue) {
        const auto trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return defaultValue;
        }
        if (trimmed.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (trimmed.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
            return false;
        }
        bool ok = false;
        const int value = trimmed.toInt(&ok);
        if (ok) {
            return value != 0;
        }
        return defaultValue;
    };

    double parsedValue = 0.0;
    if (parseLocaleDouble(element.firstChildElement("Minimum").text(), &parsedValue)) {
        setMinimum(parsedValue);
    }
    if (parseLocaleDouble(element.firstChildElement("Maximum").text(), &parsedValue)) {
        setMaximum(parsedValue);
    }

    bool ok = false;
    const int parsedDecimals = element.firstChildElement("Decimals").text().toInt(&ok);
    if (ok && parsedDecimals >= 0) {
        setDecimals(parsedDecimals);
    }

    const auto stepMode = element.firstChildElement("StepMode").text().trimmed();
    if (stepMode.compare(QStringLiteral("linear"), Qt::CaseInsensitive) == 0) {
        m_stepMode = StepMode::Linear;
    } else {
        m_stepMode = StepMode::PowerOfTwo;
    }

    if (parseLocaleDouble(element.firstChildElement("StepSize").text(), &parsedValue) &&
            parsedValue > 0.0) {
        m_stepSize = parsedValue;
    }

    m_showButtons = parseBool(element.firstChildElement("ShowButtons").text(), true);
    m_allowStepping = parseBool(element.firstChildElement("AllowStepping").text(), true);
    const bool keyboardTracking = parseBool(
            element.firstChildElement("KeyboardTracking").text(),
            false);
    setKeyboardTracking(keyboardTracking);
    setButtonSymbols(m_showButtons
                    ? QAbstractSpinBox::UpDownArrows
                    : QAbstractSpinBox::NoButtons);
}

void WBeatSpinBox::stepBy(int steps) {
    if (!m_allowStepping) {
        return;
    }

    const double oldValue = m_valueControl.get();
    double newValue = oldValue;

    if (m_stepMode == StepMode::Linear) {
        newValue = std::clamp(oldValue + (steps * m_stepSize), minimum(), maximum());
    } else {
        QString temp = text();
        int cursorPos = lineEdit()->cursorPosition();
        if (validate(temp, cursorPos) == QValidator::Acceptable) {
            newValue = valueFromText(temp) * pow(2, steps);
        }
        newValue = std::clamp(newValue, minimum(), maximum());
    }

    // Do not call QDoubleSpinBox::setValue directly in case
    // the new value of the ControlObject needs to be confirmed.
    m_valueControl.set(newValue);
    selectAll();
}

void WBeatSpinBox::slotSpinboxValueChanged(double newValue) {
    // Do not call QDoubleSpinBox::setValue directly in case
    // the new value of the ControlObject needs to be confirmed.
    m_valueControl.set(newValue);
}

void WBeatSpinBox::slotControlValueChanged(double newValue) {
    if (value() != newValue) {
        setValue(newValue);
    }
}

QString WBeatSpinBox::fractionString(int numerator, int denominator) const {
    return QString("%1/%2").arg(numerator).arg(denominator);
}

bool WBeatSpinBox::parseLocaleDouble(const QString& text, double* pOutValue) const {
    if (!pOutValue) {
        return false;
    }
    bool ok = false;
    const double value = locale().toDouble(text.trimmed(), &ok);
    if (!ok) {
        return false;
    }
    *pOutValue = value;
    return true;
}

QString WBeatSpinBox::textFromValue(double value) const {
    double dWholePart, dFracPart;
    dFracPart = modf(value, &dWholePart);

    QString sFracPart;
    if (dFracPart == 0.5) {
        sFracPart = fractionString(1, 2);
    } else if (dFracPart == 0.25) {
        sFracPart = fractionString(1, 4);
    } else if (dFracPart == 0.75) {
        sFracPart = fractionString(3, 4);
    } else if (dFracPart == 0.33333) {
        sFracPart = fractionString(1, 3);
    } else if (dFracPart == 0.66667) {
        sFracPart = fractionString(2, 3);
    } else if (dFracPart == 0.125) {
        sFracPart = fractionString(1, 8);
    } else if (dFracPart == 0.375) {
        sFracPart = fractionString(3, 8);
    } else if (dFracPart == 0.625) {
        sFracPart = fractionString(5, 8);
    } else if (dFracPart == 0.875) {
        sFracPart = fractionString(7, 8);
    } else if (dFracPart == 0.0625) {
        sFracPart = fractionString(1, 16);
    } else if (dFracPart == 0.1875) {
        sFracPart = fractionString(3, 16);
    } else if (dFracPart == 0.3125) {
        sFracPart = fractionString(5, 16);
    } else if (dFracPart == 0.4375) {
        sFracPart = fractionString(7, 16);
    } else if (dFracPart == 0.5625) {
        sFracPart = fractionString(9, 16);
    } else if (dFracPart == 0.6875) {
        sFracPart = fractionString(11, 16);
    } else if (dFracPart == 0.8125) {
        sFracPart = fractionString(13, 16);
    } else if (dFracPart == 0.9375) {
        sFracPart = fractionString(15, 16);
    } else if (dFracPart == 0.03125) {
        sFracPart = fractionString(1, 32);
    } else if (dFracPart == 0.09375) {
        sFracPart = fractionString(3, 32);
    } else if (dFracPart == 0.15625) {
        sFracPart = fractionString(5, 32);
    } else if (dFracPart == 0.21875) {
        sFracPart = fractionString(7, 32);
    } else if (dFracPart == 0.28125) {
        sFracPart = fractionString(9, 32);
    } else if (dFracPart == 0.34375) {
        sFracPart = fractionString(11, 32);
    } else if (dFracPart == 0.40625) {
        sFracPart = fractionString(13, 32);
    } else if (dFracPart == 0.46875) {
        sFracPart = fractionString(15, 32);
    } else if (dFracPart == 0.53125) {
        sFracPart = fractionString(17, 32);
    } else if (dFracPart == 0.59375) {
        sFracPart = fractionString(19, 32);
    } else if (dFracPart == 0.65625) {
        sFracPart = fractionString(21, 32);
    } else if (dFracPart == 0.71875) {
        sFracPart = fractionString(23, 32);
    } else if (dFracPart == 0.78125) {
        sFracPart = fractionString(25, 32);
    } else if (dFracPart == 0.84375) {
        sFracPart = fractionString(27, 32);
    } else if (dFracPart == 0.90625) {
        sFracPart = fractionString(29, 32);
    } else if (dFracPart == 0.96875) {
        sFracPart = fractionString(31, 32);
    } else {
        return locale().toString(value, 'g', 5);
    }

    if (dWholePart > 0) {
        return locale().toString(dWholePart, 'f', 0) + " " + sFracPart;
    }
    return sFracPart;
}

double WBeatSpinBox::valueFromText(const QString& text) const {
    if (text.count(" ") > 1 || text.count("/") > 1) {
        return value();
    }

    bool conversionWorked = false;
    double dValue;
    dValue = locale().toDouble(text, &conversionWorked);
    if (conversionWorked) {
        return dValue;
    }

    QString sIntPart, sFracPart, sNumerator, sDenominator;

    if (text.contains(" ")) {
        QStringList numberParts = text.split(" ");
        sIntPart = numberParts.at(0);
        sFracPart = numberParts.at(1);
    } else if (text.contains("/")) {
        sFracPart = text;
    }

    QStringList splitFraction = sFracPart.split("/");
    sNumerator = splitFraction.at(0);
    sDenominator = splitFraction.at(1);

    return locale().toDouble(sIntPart)
            + locale().toDouble(sNumerator) / locale().toDouble(sDenominator);
}

QValidator::State WBeatSpinBox::validate(QString& input, int& pos) const {
    Q_UNUSED(pos);
    QRegularExpressionMatch blockListMatch = kBlockListRegex.match(input);
    if (blockListMatch.hasMatch()) {
        return QValidator::Invalid;
    }
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }

    if (input.count(" ") > 1 || input.count("/") > 1) {
        return QValidator::Invalid;
    }

    bool conversionWorked = false;
    locale().toDouble(input, &conversionWorked);
    if (conversionWorked) {
        return QValidator::Acceptable;
    }

    QString sIntPart, sFracPart, sNumerator, sDenominator;

    if (input.contains(" ")) {
        // Whole number + fraction, for example "1 1/2"
        QStringList numberParts = input.split(" ");
        sIntPart = numberParts.at(0);
        sFracPart = numberParts.at(1);
        // Whole number + trailing space
        if (sFracPart.isEmpty()) {
            return QValidator::Intermediate;
        }
    } else if (input.contains("/")) {
        // Fraction without whole number, for example "1/2"
        sFracPart = input;
    }

    if (!sIntPart.isEmpty()) {
        conversionWorked = false;
        sIntPart.toInt(&conversionWorked);
        if (!conversionWorked) {
            return QValidator::Invalid;
        }
    }

    if (!sFracPart.contains("/")) {
        return QValidator::Intermediate;
    }
    QStringList fracParts = sFracPart.split("/");
    sNumerator = fracParts.at(0);
    if (sNumerator.isEmpty()) {
        // when deleting the numerator, for example "/32"
        return QValidator::Intermediate;
    }
    sDenominator = fracParts.at(1);

    conversionWorked = false;
    sNumerator.toInt(&conversionWorked);
    if (!conversionWorked) {
        return QValidator::Invalid;
    }

    if (sDenominator.isEmpty()) {
        return QValidator::Intermediate;
    }
    conversionWorked = false;
    sDenominator.toInt(&conversionWorked);
    if (!conversionWorked) {
        return QValidator::Invalid;
    }

    return QValidator::Acceptable;
}

bool WBeatSpinBox::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Change the new font on the fly by casting away its constancy
        // using setFont() here, would results into a recursive loop
        // resetting the font to the original css values.
        // Only scale pixel size fonts, point size fonts are scaled by the OS
        // This font instance is only used for size measuring in
        // QAbstractSpinBox::minimumSizeHint() the lineEdit()->font() is used for
        // rendering
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
    }
    return QDoubleSpinBox::event(pEvent);
}

void WBeatSpinBox::keyPressEvent(QKeyEvent* pEvent) {
    // By default, Return & Enter keys apply the current value.
    // Additionally, move focus back to the previously focused library widget.
    if (pEvent->key() == Qt::Key_Return ||
            pEvent->key() == Qt::Key_Enter ||
            pEvent->key() == Qt::Key_Escape) {
        QDoubleSpinBox::keyPressEvent(pEvent);
        ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
        return;
    }

    if (!m_allowStepping &&
            (pEvent->key() == Qt::Key_Up ||
                    pEvent->key() == Qt::Key_Down ||
                    pEvent->key() == Qt::Key_PageUp ||
                    pEvent->key() == Qt::Key_PageDown)) {
        pEvent->accept();
        return;
    }

    QDoubleSpinBox::keyPressEvent(pEvent);
}

void WBeatSpinBox::wheelEvent(QWheelEvent* pEvent) {
    if (!m_allowStepping) {
        pEvent->accept();
        return;
    }
    QDoubleSpinBox::wheelEvent(pEvent);
}

bool WBeatLineEdit::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Change the new font on the fly by casting away its constancy
        // using setFont() here, would results into a recursive loop
        // resetting the font to the original css values.
        // Only scale pixel size fonts, point size fonts are scaled by the OS
        // This font instance is the one, used for rendering.
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
    }
    return QLineEdit::event(pEvent);
}
