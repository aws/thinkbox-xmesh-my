// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <maya/MMessage.h>
#include <maya/MPxLocatorNode.h>
#include <maya/MTime.h>

#include <frantic/files/filename_sequence.hpp>
#include <frantic/geometry/polymesh3.hpp>
#include <frantic/graphics/boundbox3f.hpp>

#include <xmesh/cached_polymesh3_loader.hpp>

namespace frantic {
namespace geometry {

class trimesh3;

}
} // namespace frantic

enum clamp_mode {
    CLAMP_MODE_HOLD = 1,
    CLAMP_MODE_BLANK,
};

enum clamp_region { CLAMP_REGION_INSIDE, CLAMP_REGION_BEFORE, CLAMP_REGION_AFTER };

enum seq_ID {
    SEQ_RENDER = 1,
    SEQ_PROXY,
};

enum display_mode {
    DISPLAY_MODE_MESH = 1,
    DISPLAY_MODE_BOX,
    DISPLAY_MODE_VERTEX,
    DISPLAY_MODE_FACE,
};

enum scene_units {
    UNITS_GENERIC = 1,
    UNITS_CM,
    UNITS_MM,
    UNITS_M,
    UNITS_IN,
    UNITS_FT,
    UNITS_KM,
    UNITS_MILES,
    UNITS_CUSTOM,
};

enum load_mode {
    LOADMODE_STATIC,
    LOADMODE_BLANK,
    LOADMODE_VELOCITY_OFFSET,
    LOADMODE_SUBFRAME_VELOCITY_OFFSET,
    LOADMODE_FRAME_INTERPOLATION,
    LOADMODE_SUBFRAME_INTERPOLATION,
};

// void beginRender( void* cliendData);
// void endRender( void* cliendData);

class SequenceXMeshNode : public MPxLocatorNode {
  public:
    static MObject seqPath;
    static MObject seqProxyPath;

    static MObject outMesh;

    static MObject inTime;
    static MObject inGroupIds;
    static MObject inPlaybackGraph;
    static MObject inEnablePlaybackGraph;
    static MObject inFrameOffset;
    static MObject inUseCustomRange;
    static MObject inCustomRangeStart;
    static MObject inCustomRangeEnd;
    static MObject inCustomRangeStartClampMode;
    static MObject inCustomRangeEndClampMode;
    static MObject inSingleFileOnly; // loading mode?

    static MObject inCustomScale;
    static MObject inLengthUnit;
    static MObject inLoadingMode;
    static MObject inAutoProxyPath;

    static MObject inViewportSource;
    static MObject inDisplayMode;
    static MObject inDisplayPercent;
    static MObject inRenderSource;
    static MObject inRender;

    static MObject outMinimumAvailableFileIndex;
    static MObject outMaximumAvailableFileIndex;

    static MTypeId typeID;
    static MString drawClassification;
    static MString drawRegistrantId;

  private:
    MCallbackId m_computeViewportCallbackId;
    MCallbackId m_computeRenderCallbackId;

    frantic::files::filename_sequence m_cachedFilenameSequence;
    frantic::files::filename_sequence m_cachedProxyFilenameSequence;

    // which filename_sequence path was used to sync_frame_set()
    frantic::tstring m_cachedFilenameSequencePath;
    frantic::tstring m_cachedProxyFilenameSequencePath;

    frantic::graphics::boundbox3f m_meshBoundingBox;
    frantic::geometry::const_polymesh3_ptr m_cachedPolymesh3;

    std::pair<frantic::geometry::const_polymesh3_ptr, frantic::geometry::const_polymesh3_ptr> m_cachedPolymesh3Interval;

    xmesh::cached_polymesh3_loader m_polymesh3Loader;

    double m_cachedFrame;
    std::pair<double, double> m_cachedInterval;
    frantic::tstring m_cachedFilenamePattern; // what filename pattern does the cache represent?
    bool m_cachedUseFirst;                    // first or second in range;
    load_mode m_cachedLoadingMode;            // which loading mode was the cache constructed in?
    int m_cachedLoadMask;                     // which load mask was used to load the mesh?

    // local data
    frantic::geometry::xmesh_metadata m_metadata;

    MBoundingBox m_boundingBox;

    static boost::shared_ptr<frantic::geometry::trimesh3> g_iconMesh;

    // methods
    // void	handle_custom_range( load_mode& loadMode, MTime &requestTime, MTime inTime, MTime inPivotTime);

    // void clear_cache();//?

    // load_mode get_load_mode_from_clamp_mode( clamp_mode clampMode );
    //  these allow you to grab the nearest subframe/wholeframe from a sequence
    //  they return false when an appropriate frame can't be found
    // bool		get_nearest_subframe( float time, double &frameNumber );  //?
    // bool		get_nearest_wholeframe( float time, double &frameNumber );//?
    // int		round_to_nearest_wholeframe( MTime t ) const;

    // cached mesh info utility functions
    // void invalidate_cache();//?
    // check for a frame in the cache at time t
    // bool frame_exists( float t );//?!

    // load a pair of meshes, suitable for interpolation, into the m_cachedPolymesh3Interval
    void load_mesh_interval( seq_ID seqId, std::pair<double, double> interval, int loadMask );

    // loads a mesh into the locally cached polymesh
    void load_mesh_at_frame( seq_ID seqId, double frame, int loadMask );

    // void build_channel_assignment( mesh_channel_assignment& channels, bool useVelocity, float timeOffset, float
    // timeDerivative, const frantic::graphics::transform4f & xform );

    // HELPERS
    frantic::tstring get_sequence_path( seq_ID seqId );
    frantic::files::filename_sequence& get_sequence( seq_ID seqId, bool throwIfMissing = true ); //?
    // frantic::tstring							get_current_sequence_path();//?
    // frantic::tstring							get_render_sequence_path();//?
    // frantic::files::filename_sequence&		get_render_sequence();//?
    // int										get_current_display_mode();//?
    display_mode get_effective_display_mode();
    frantic::tstring get_render_path();
    frantic::tstring get_proxy_path();

    // void										set_to_valid_frame_range( bool notify = false, bool setLoadSingleFrame = false
    // );
    bool is_autogen_proxy_path_enabled();
    frantic::tstring get_auto_proxy_path();
    frantic::tstring get_auto_proxy_directory();
    void check_auto_proxy_path();

    clamp_mode get_start_clamp_mode() const;
    clamp_mode get_end_clamp_mode() const;

    // void build_normals();//?

    void cache_bounding_box();

  public:
    static void* creator();
    static MStatus initialize();

    virtual MBoundingBox boundingBox() const;
    virtual bool isBounded() const;
    virtual void draw( M3dView& view, const MDagPath& path, M3dView::DisplayStyle style,
                       M3dView::DisplayStatus status );

#if MAYA_API_VERSION >= 201800
    bool getInternalValue( const MPlug&, MDataHandle& ) override;
    bool setInternalValue( const MPlug&, const MDataHandle& ) override;
#else
    virtual bool getInternalValueInContext( const MPlug& plug, MDataHandle& dataHandle, MDGContext& currentContext );
    virtual bool setInternalValueInContext( const MPlug& plug, const MDataHandle& dataHandle,
                                            MDGContext& currentContext );
#endif

    const frantic::graphics::boundbox3f& get_mesh_bounding_box();
    display_mode get_display_mode();
    float get_display_fraction();

    virtual MStatus compute( const MPlug& plug, MDataBlock& data );

    void postConstructor();
    SequenceXMeshNode();
    ~SequenceXMeshNode();

    static boost::shared_ptr<frantic::geometry::trimesh3> get_icon_mesh();
    frantic::geometry::const_polymesh3_ptr get_cached_mesh();
};
