#include "backend.h"

void Backend::primaryAction()
{
    ++m_count;
    emit countChanged();
}
