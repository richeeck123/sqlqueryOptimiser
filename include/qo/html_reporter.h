#pragma once

/**
 * HtmlReporter — generates a self-contained multi-profile HTML optimizer dashboard.
 *
 * Usage
 * ─────
 *   std::vector<qo::HtmlReporter::QueryProfile> profiles;
 *   profiles.push_back({ "Profile Title", sqlQuery, initialPlan, rboPlan, cboPlan,
 *                         initialCost, finalCost });
 *   HtmlReporter::generateDashboard("optimizer_dashboard.html", profiles);
 *
 * The output is a fully self-contained single-page HTML document with inline CSS
 * and JavaScript.  It renders a premium dark-mode developer dashboard with:
 *   - A styled profile selector for switching between query scenarios
 *   - Cost metrics (initial cost, optimised cost, performance boost %)
 *   - Animated side-by-side comparison of all three plan tree representations
 *   - Client-side syntax highlighting of plan tree keywords
 */

#include <string>
#include <vector>

namespace qo {

class HtmlReporter {
public:
    // ── Profile data ──────────────────────────────────────────────────────────

    /// All data for a single query optimisation scenario.
    struct QueryProfile {
        std::string title;        ///< Short display name shown in the selector
        std::string sqlQuery;     ///< The original SQL query text
        std::string initialPlan;  ///< Plan dump before any optimisation
        std::string rboPlan;      ///< Plan dump after predicate push-down (RBO)
        std::string cboPlan;      ///< Plan dump after join reordering (CBO)
        std::string optimizedQuery; ///< SQL reconstructed from final optimised plan
        bool appliedPredicatePushdown; ///< Badge flag
        bool appliedJoinReordering;    ///< Badge flag
        bool cardinalityReductionAchieved; ///< Badge flag
        bool appliedOrUnionExpansion;  ///< Badge flag
        long initialCost;  ///< Estimated cost before optimisation
        long finalCost;    ///< Estimated cost after CBO optimisation
    };

    // ── Public API ────────────────────────────────────────────────────────────

    /// Generate a self-contained HTML dashboard file at 'filename'.
    ///
    /// @param filename  Output file path (e.g. "optimizer_dashboard.html")
    /// @param profiles  One or more query optimisation scenarios to embed
    ///
    /// @throws std::runtime_error if the output file cannot be opened or
    ///         if 'profiles' is empty.
    static void generateDashboard(const std::string&              filename,
                                  const std::vector<QueryProfile>& profiles);

private:
    /// Escape HTML special characters in 'text' for safe inline embedding.
    static std::string htmlEscape(const std::string& text);

    /// Escape a string for safe embedding inside a JavaScript string literal.
    static std::string jsEscape(const std::string& text);
};

} // namespace qo
