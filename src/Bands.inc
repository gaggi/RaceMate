// Rows shared by the overlay pages: rounded corners under Layout, and header and footer,
// each with a switch and its four metric slots in the expanded row.
std::wstring SlotsText(const Slots& metrics) {
    std::wstring text;
    for(int slot=0;slot<SlotCount;++slot) text+=(slot?L"  \u00B7  ":L"")+std::wstring(metrics[slot]==NoMetric?L"\u2014":MetricTitles[metrics[slot]]);
    return text;
}
RowList::Row RoundedRow(const Overlay& o) {
    RowList::Row row; row.title=L"Rounded corners"; row.detail=L"Off: square corners."; row.toggle=o.place.rounded?1:0; return row;
}
void SetRounded(Overlay& o,bool on) { o.place.rounded=on; Fit(o); }
RowList::Row BandRow(const Overlay& o,bool footer) {
    RowList::Row row; row.title=footer?L"Footer":L"Header";
    row.detail=SlotsText(footer?o.bands.footer.metrics:o.bands.headerMetrics); row.toggle=(footer?o.bands.footer.enabled:o.bands.header)?1:0;
    return row;
}
void SetBand(Overlay& o,bool footer,bool on) { (footer?o.bands.footer.enabled:o.bands.header)=on; }
// The four slots of an expanded header or footer row, as combo boxes firstId to firstId+3.
void BandEditors(RowEditors& editors,const Overlay& o,bool footer,int firstId) {
    const Slots& metrics=footer?o.bands.footer.metrics:o.bands.headerMetrics;
    const std::vector<std::wstring> titles(std::begin(MetricTitles),std::end(MetricTitles));
    const wchar_t* labels[]={L"Left",L"Right 1",L"Right 2",L"Right 3"};
    for(int slot=0;slot<SlotCount;++slot) { editors.Label(labels[slot],slot*184,10,56); editors.Combo(firstId+slot,titles,metrics[slot],slot*184+58,10,118); }
}
void ApplyBandEditor(RowEditors& editors,Overlay& o,bool footer,int id,int firstId) {
    (footer?o.bands.footer.metrics:o.bands.headerMetrics)[id-firstId]=std::clamp(editors.Selection(id),0,MetricCount-1);
}
