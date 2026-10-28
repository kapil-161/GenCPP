#ifndef SPEGRAPHBUILDER_H
#define SPEGRAPHBUILDER_H

#include <QStringList>
#include "SpeGraphWidget.h"

// Builds the graph for the SPE line at `lineIndex` (0-based) from the file's
// lines: 4-value response functions, X/Y line pairs, @-header tables and the
// CROPGRO plant composition section. Returns false when the line has no graph.
// Kept free of the editor so --spe-report can run it over every SPE file.
namespace SpeGraphBuilder {
    bool build(const QStringList &lines, int lineIndex, SpeGraphData &out);
}

#endif // SPEGRAPHBUILDER_H
