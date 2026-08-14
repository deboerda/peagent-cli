#pragma once

#include "HelloMCPChartSupport.h"


namespace hello_mcp { namespace detail {

class ChartObjectService {
public:
	static json CreateRemark(const json& config);
	static json CreateCurve(const json& config);
	static json CreateNumber(const json& config);
	static json CreateProgress(const json& config);
	static json CreateHistogram(const json& config);
	static json CreateTable(const json& config);
	static json CreateGrid(const json& config);
	static json CreateDashBoard(const json& config);

	static bool ApplyRemark(CRemark* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyCurve(CCurve* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyNumber(CNumber* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyProgress(CProgress* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyHistogram(CHistogram* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyTable(CTable* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyGrid(CGrid* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool ApplyDashBoard(CDashBoard* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);

private:
	static bool RejectRename(const json& data, const std::string& objectType,
		std::string& errorMessage);
};

} }
