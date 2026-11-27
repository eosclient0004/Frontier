/*============================================================================================================================================
                                                              REVISIONCODEC.CPP
============================================================================================================================================*/
// 🧩 Binary .Baseline and .Revision section serialization implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Serialization/Container/BinaryFormatPrimitives.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Codecs
{

namespace
{

constexpr uint32_t BaselineBinarySignature = 0x4C534246u; // 'FBSL'
constexpr uint32_t RevisionBinarySignature = 0x56455246u; // 'FREV'



} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              REVISION CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void RevisionEncoder::AttachRevisionSections(
    const Workspaces::RevisionTracker& Tracker,
    SpaceContainerPayload&             InOutPayload)
{
    // Remove any existing .Baseline or .Revision sections so re-saving replaces them cleanly.
    InOutPayload.BinarySections.erase(
        std::remove_if(
            InOutPayload.BinarySections.begin(),
            InOutPayload.BinarySections.end(),
            [](const BinarySectionBlock& Existing)
            {
                return Existing.SubFormatTag == ".Baseline" || Existing.SubFormatTag == ".Revision";
            }),
        InOutPayload.BinarySections.end());

    const Workspaces::OperationRecorder&         Recorder = Tracker.InspectRecorder();
    const Workspaces::ParametricBaselineSummary& Baseline = Recorder.InspectBaseline();
    const Workspaces::NonLinearHistory&          History  = Tracker.InspectHistory();

    // 1. Encode .Baseline binary section.
    BinarySectionBlock BaselineBlock{};
    BaselineBlock.SectionIdentifier = "ParametricBaseline";
    BaselineBlock.SubFormatTag      = ".Baseline";
    Frontier::Serialization::AppendPodBytes(BaselineBlock.PayloadBytes, BaselineBinarySignature);
    Frontier::Serialization::AppendPodBytes(BaselineBlock.PayloadBytes, Baseline.FoldedOperationCount);
    Frontier::Serialization::AppendPodBytes(BaselineBlock.PayloadBytes, Baseline.CumulativeExposureEV);
    const uint32_t FoldedListCount = static_cast<uint32_t>(Baseline.FoldedOperations.size());
    Frontier::Serialization::AppendPodBytes(BaselineBlock.PayloadBytes, FoldedListCount);
    for (const Workspaces::ParametricOperationParameters& Op : Baseline.FoldedOperations)
    {
        Frontier::Serialization::AppendPodBytes(BaselineBlock.PayloadBytes, Op);
    }
    BaselineBlock.Checksum64 = SpaceContainerEncoder::ComputeFnv1aChecksum64(BaselineBlock.PayloadBytes);
    InOutPayload.BinarySections.push_back(std::move(BaselineBlock));

    // 2. Encode .Revision binary section (HistoryStepEntry array + ParametricRecordEntry array + ContentRegion bytes).
    BinarySectionBlock RevisionBlock{};
    RevisionBlock.SectionIdentifier = "ParametricRevisionTree";
    RevisionBlock.SubFormatTag      = ".Revision";
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, RevisionBinarySignature);
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, History.QueryRootIndex());
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, History.QueryCursorIndex());
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, Tracker.QuerySavedStepIndex());
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, History.QueryNextLocalOrdinal());

    const auto     HistoryEntries = History.InspectEntries();
    const uint32_t HistoryCount   = static_cast<uint32_t>(HistoryEntries.size());
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, HistoryCount);
    for (const Workspaces::HistoryStepEntry& Step : HistoryEntries)
    {
        Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, Step);
    }

    const auto     Records     = Recorder.InspectRecordEntries();
    const uint32_t RecordCount = static_cast<uint32_t>(Records.size());
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, RecordCount);
    for (const Workspaces::ParametricRecordEntry& Entry : Records)
    {
        Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, Entry);
    }

    const auto     RegionBytes = Recorder.InspectContentRegion().InspectAllBytes();
    const uint64_t RegionSize  = static_cast<uint64_t>(RegionBytes.size());
    Frontier::Serialization::AppendPodBytes(RevisionBlock.PayloadBytes, RegionSize);
    if (!RegionBytes.empty())
    {
        const std::size_t Start = RevisionBlock.PayloadBytes.size();
        RevisionBlock.PayloadBytes.resize(Start + RegionBytes.size());
        std::memcpy(RevisionBlock.PayloadBytes.data() + Start, RegionBytes.data(), RegionBytes.size());
    }
    RevisionBlock.Checksum64 = SpaceContainerEncoder::ComputeFnv1aChecksum64(RevisionBlock.PayloadBytes);
    InOutPayload.BinarySections.push_back(std::move(RevisionBlock));
}

bool RevisionDecoder::ExtractRevisionSections(
    const SpaceContainerPayload& Payload,
    Workspaces::RevisionTracker& OutTracker,
    std::string&                 OutDiagnostic)
{
    const BinarySectionBlock* BaselineSection = nullptr;
    const BinarySectionBlock* RevisionSection = nullptr;
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".Baseline")
        {
            BaselineSection = &Section;
        }
        else if (Section.SubFormatTag == ".Revision")
        {
            RevisionSection = &Section;
        }
    }

    if (BaselineSection == nullptr && RevisionSection == nullptr)
    {
        OutTracker.Reset();
        return true;
    }
    if (BaselineSection == nullptr || RevisionSection == nullptr)
    {
        OutDiagnostic = "Container must include both .Baseline and .Revision sections when persisting revision history.";
        return false;
    }

    // 1. Decode .Baseline
    Workspaces::ParametricBaselineSummary DecodedBaseline{};
    {
        std::size_t Cursor    = 0u;
        uint32_t    Signature = 0u;
        uint32_t    FoldCount = 0u;
        if (!Frontier::Serialization::ReadPodBytes(BaselineSection->PayloadBytes, Cursor, Signature) || Signature != BaselineBinarySignature ||
            !Frontier::Serialization::ReadPodBytes(BaselineSection->PayloadBytes, Cursor, DecodedBaseline.FoldedOperationCount) ||
            !Frontier::Serialization::ReadPodBytes(BaselineSection->PayloadBytes, Cursor, DecodedBaseline.CumulativeExposureEV) ||
            !Frontier::Serialization::ReadPodBytes(BaselineSection->PayloadBytes, Cursor, FoldCount))
        {
            OutDiagnostic = "Corrupted .Baseline binary section header.";
            return false;
        }
        DecodedBaseline.FoldedOperations.resize(FoldCount);
        for (uint32_t Index = 0u; Index < FoldCount; ++Index)
        {
            if (!Frontier::Serialization::ReadPodBytes(BaselineSection->PayloadBytes, Cursor, DecodedBaseline.FoldedOperations[Index]))
            {
                OutDiagnostic = "Truncated .Baseline folded parametric operation array.";
                return false;
            }
        }
    }

    // 2. Decode .Revision
    std::size_t Cursor           = 0u;
    uint32_t    Signature        = 0u;
    uint32_t    RootIndex        = 0u;
    uint32_t    CursorIndex      = 0u;
    uint32_t    SavedIndex       = 0u;
    uint32_t    NextLocalOrdinal = 1u;
    uint32_t    HistoryCount     = 0u;
    if (!Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, Signature) || Signature != RevisionBinarySignature ||
        !Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, RootIndex) ||
        !Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, CursorIndex) ||
        !Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, SavedIndex) ||
        !Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, NextLocalOrdinal) ||
        !Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, HistoryCount))
    {
        OutDiagnostic = "Corrupted .Revision binary section header.";
        return false;
    }

    std::vector<Workspaces::HistoryStepEntry> HistoryEntries(HistoryCount);
    for (uint32_t Index = 0u; Index < HistoryCount; ++Index)
    {
        if (!Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, HistoryEntries[Index]))
        {
            OutDiagnostic = "Truncated .Revision HistoryStepEntry array.";
            return false;
        }
    }

    uint32_t RecordCount = 0u;
    if (!Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, RecordCount))
    {
        OutDiagnostic = "Missing RecordCount in .Revision binary section.";
        return false;
    }

    std::vector<Workspaces::ParametricRecordEntry> Records(RecordCount);
    for (uint32_t Index = 0u; Index < RecordCount; ++Index)
    {
        if (!Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, Records[Index]))
        {
            OutDiagnostic = "Truncated .Revision ParametricRecordEntry array.";
            return false;
        }
    }

    uint64_t RegionByteSize = 0ull;
    if (!Frontier::Serialization::ReadPodBytes(RevisionSection->PayloadBytes, Cursor, RegionByteSize) ||
        RegionByteSize > (RevisionSection->PayloadBytes.size() - Cursor))
    {
        OutDiagnostic = "Invalid ContentRegion byte length in .Revision section.";
        return false;
    }

    const std::span<const std::byte> RegionSpan(
        RevisionSection->PayloadBytes.data() + Cursor,
        static_cast<std::size_t>(RegionByteSize));

    if (!OutTracker.RestoreSerializedRevision(
            DecodedBaseline,
            Records,
            RegionSpan,
            HistoryEntries,
            RootIndex,
            CursorIndex,
            SavedIndex,
            NextLocalOrdinal))
    {
        OutDiagnostic = "RevisionTracker budget exceeded while restoring .Revision section.";
        return false;
    }

    return true;
}

} // namespace Frontier::Codecs
